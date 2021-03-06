/*===========================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ===========================================================================
*
*/

#ifndef _ReadsGetter_
#define _ReadsGetter_

#include <ngs/ncbi/NGS.hpp>
#include <ngs/ErrorMsg.hpp>
#include <ngs/ReadCollection.hpp>
#include <ngs/ReadIterator.hpp>
#include <ngs/Read.hpp>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/seek.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/regex.hpp>

#include "DBGraph.hpp"

namespace DeBruijn {

//  CReadsGetter gets reads from SRA, fasta, or fastq files. It finds the leftmost longest
//  subsequence of unambiguous characters from reads and stores them in list<array<CReadHolder,2>>.
//  Paired and unpaired reads are kept in different elements of the array (0 and 1).
//
//  The input data is validated and an exception is thrown in case of error.
//  Validation for SRA: done by NGS library
//  Format validation for fasta: '>' starts defline
//  Format validation for fastq: '@' and '+' start first and third lines in every block of four lines
//  Validation for paired reads with input from fasta or fastq: mates have same prefix; suffix is
//                               '[./][12]' with 1 and 2 for first and second mate, respectively
//  Exception messages produced are for error opening file, invalid file format, no valid reads in a
//  specific input source or in all sources available for assembly, read sequence is invalid, and paired
//  input contains different number of mates.

    class CReadsGetter {
    public:


//      sra_list, fasta_list, fastq_list - input reads from SRA accessions or files in fasta or fastq format.
//           Files for paired reads should have mates interleaved (first followed by second) or should be in two separate
//           files specified as a list separated by comma with file for first mate followed by the file for second mate.
//      ncores - number of cores
//      usepairedends - flag to indicate that input reads are paired
//      gzipped - flag to indicate that input files are gzipped

        CReadsGetter(const vector<string>& sra_list, const vector<string>& fasta_list, const vector<string>& fastq_list, int ncores, bool usepairedends, bool gzipped) : 
            m_ncores(ncores), m_usepairedends(usepairedends), m_gzipped(gzipped) {

            CStopWatch timer;
            timer.Restart();


            if(!fasta_list.empty())
                ReadFastaOrFastq(fasta_list, true);
            if(!fastq_list.empty())
                ReadFastaOrFastq(fastq_list, false);
            if(!sra_list.empty())
                GetFromSRA(sra_list);

            int total = 0;
            int paired = 0;
            for(auto& reads : m_reads) {


                total += reads[0].ReadNum()+reads[1].ReadNum();
                paired += reads[0].ReadNum();
            }

            if(total == 0)
                throw runtime_error("No valid reads available for assembly");
    
            if(m_usepairedends)
                cerr << "Total mates: " << total << " Paired reads: " << paired/2 << endl;
            else
                cerr << "Total reads: " << total << endl;
            cerr << "Reads acquired in " << timer.Elapsed();
        }
        virtual ~CReadsGetter() {}

        list<array<CReadHolder,2>>& Reads() { return m_reads; }
    
    private:

        // insert read from source_name to rholder
        static void InsertRead(string& read, CReadHolder& rholder, const string& source_name) {
            //convert to upper case
            for(char& c : read) c = toupper(c);
            //check if read is valid
            if(read.find_first_not_of("ACGTYRWSKMDVHBXN-") != string::npos)
                throw runtime_error("Invalid sequence in "+source_name);           
            size_t best_start = 0;
            int best_len = 0;
            size_t start = 0;

            // find and store the leftmost longest unambiguous stretch of read
            while(start < read.size()) {
                size_t stop = min(read.size(),read.find_first_not_of("ACGT", start));
                int len = stop-start;
                if(len > best_len) {
                    best_len = len;
                    best_start = start;
                }
                start = read.find_first_of("ACGT", stop);
            }
            if(best_len > 0) 
                rholder.PushBack(read.substr(best_start, best_len));
            else
                rholder.PushBack("");  // keep a bogus read for paired  
        }

        typedef tuple<string,size_t,size_t> TSlice;
        typedef list<TSlice> TReadJob;

//      total_length - total number of reads in all runs (sum of file_length)
//      job_length - desired number of reads in one job
//      file_list  - run names
//      file_length - run sizes
//      job_inputs - created jobs (lists of name,from,to)

        static void ReadJobInputs(size_t total_length, size_t job_length,  const vector<string>& file_list, const vector<size_t>& file_length, list<TReadJob>& job_inputs) {
            size_t assigned_length = 0;
            int file_num = 0;
            size_t assigned_length_from_file = 0;
            while(assigned_length < total_length) {
                job_inputs.push_back(TReadJob());
                size_t current_job_length = 0;
                while(current_job_length < job_length && assigned_length < total_length) {
                    size_t max_chunk = file_length[file_num] - assigned_length_from_file;
                    if(current_job_length + max_chunk <= job_length) {  // the rest of the file could be assigned           
                        job_inputs.back().push_back(TSlice(file_list[file_num], assigned_length_from_file, file_length[file_num]-1));
                        assigned_length += max_chunk;
                        current_job_length += max_chunk;
                        ++file_num;
                        assigned_length_from_file = 0;
                    } else {   // something left for another job            
                        size_t chunk = job_length - current_job_length;
                        job_inputs.back().push_back(TSlice(file_list[file_num], assigned_length_from_file, assigned_length_from_file+chunk-1));
                        assigned_length_from_file += chunk;
                        assigned_length += chunk;
                        current_job_length = job_length;
                    }
                }
            }
        }

        // A one-thread worker to get reads from SRA
        // job - accession(s) and interval(s) to get
        // rslt - destination
        
        static void GetFromSRAJob(const TReadJob job, array<CReadHolder,2>& rslt) {  // job must be by value - it is deleted in the caller     
            using namespace ngs;
            for(auto& slice : job) {
                const string& acc = get<0>(slice);
                ReadCollection run = ncbi::NGS::openReadCollection (acc);
                size_t from = get<1>(slice);
                size_t to = get<2>(slice);
                ReadIterator it = run.getReadRange (from+1, to-from+1, Read::all);
                while (it.nextRead()) {
                    int fragments = it.getNumFragments ();
                    if(fragments == 2) { // paired read
                        it.nextFragment();
                        StringRef s1 = it.getFragmentBases();
                        int read_length1 = s1.size();
                        string read1 = string(s1.data(),read_length1);

                        it.nextFragment();
                        StringRef s2 = it.getFragmentBases();
                        int read_length2 = s2.size();
                        string read2 = string(s2.data(),read_length2);

                        InsertRead(read1, rslt[0], acc);
                        InsertRead(read2, rslt[0], acc);                    
                                    
                    } else {             // unpaired read
                        while(it.nextFragment()) {
                            StringRef s = it.getFragmentBases();
                            int read_length = s.size();
                            string read = string(s.data(),read_length);
                            InsertRead(read, rslt[1], acc);
                        } 
                    }               
                }
            }
        }

        // Acquires reads from SRA
        // sra_list - run accessions
        void GetFromSRA(const vector<string>& sra_list) {
            using namespace ngs;
            vector<size_t> file_length;
            size_t total_length = 0;
            for(const string& file : sra_list) {
                ReadCollection run = ncbi::NGS::openReadCollection (file);
                file_length.push_back(run.getReadCount());
                total_length += file_length.back();
            }

            list<TReadJob> job_inputs;
            size_t job_length = total_length/m_ncores+1;
            ReadJobInputs(total_length, job_length, sra_list, file_length, job_inputs);
            list<function<void()>> jobs;
            for(auto& job_input : job_inputs) {
                m_reads.push_back({CReadHolder(true), CReadHolder(false)});
                jobs.push_back(bind(GetFromSRAJob, job_input, ref(m_reads.back())));
            }
            RunThreads(m_ncores, jobs);
        }

        // Acquires reads from fasta or fastq
        // file_list - file names (could be separated by comma for paired reads)
        // isfasta - true for fasta file(s)
        void ReadFastaOrFastq(const vector<string>& file_list, bool isfasta) {
            auto NextRead = [] (string& acc, string& read, bool isfasta, boost::iostreams::filtering_istream& is, const string& source_name) {
                acc.clear();
                read.clear();

                if(isfasta) {// fasta   
                    string record;
                    if(!getline(is, record, '>'))
                        return false;
                    size_t first_ret = min(record.size(),record.find('\n'));
                    if(first_ret == string::npos)
                        throw runtime_error("Invalid fasta file format in "+source_name);
                    acc = record.substr(0, first_ret);
                    read = record.substr(first_ret+1);
                    read.erase(remove(read.begin(),read.end(),'\n'),read.end());            
                } else { // fastq 
                    if(!getline(is, acc))
                        return false;
                    if(acc[0] != '@')
                        throw runtime_error("Invalid fastq file format in "+source_name);
                    if(!getline(is, read))
                        throw runtime_error("Invalid fastq file format in "+source_name);
                    string line;
                    if(!getline(is, line) || line[0] != '+')
                        throw runtime_error("Invalid fastq file format in "+source_name);
                    if(!getline(is, line))
                        throw runtime_error("Invalid fastq file format in "+source_name);
                }                

                acc = acc.substr(0, acc.find_first_of(" \t"));

                return true;
            };

            auto OpenStream = [] (const string& file, bool gzipped, bool isfasta, boost::iostreams::filtering_istream& is) {
                boost::iostreams::file_source f{file};
                if(!f.is_open())
                    throw runtime_error("Error opening "+file);
                if(gzipped)
                    is.push(boost::iostreams::gzip_decompressor());
                is.push(f);

                // do a quick check of validity on first character of the file
                char c;
                if(isfasta) {
                    if(!(is >> c) || c != '>')
                        throw runtime_error("Invalid fasta file format in "+file);
                } else {
                    if(!(is >> c) || c != '@')
                        throw runtime_error("Invalid fastq file format in "+file);
                    is.putback(c);
                }                
            };

            // checks if ids for paired reads are name[./]1 and name[./]2
            auto MatchIds = [] (const string& acc1, const string& acc2) {
                boost::regex re1("(.+)[./]1");
                boost::cmatch matches1;
                boost::regex re2("(.+)[./]2");
                boost::cmatch matches2;
                return (acc1 == acc2 || (boost::regex_match(acc1.c_str(), matches1, re1) && boost::regex_match(acc2.c_str(), matches2, re2) && matches1[1] == matches2[1]));
            };

            array<CReadHolder,2> all_reads = {CReadHolder(true), CReadHolder(false)};
            string acc1;
            string read1;                
            string acc2;
            string read2;
            for(const string& file : file_list) {
                size_t total = all_reads[0].ReadNum()+all_reads[1].ReadNum();
                size_t comma = file.find(',');
                if(comma == string::npos) {
                    boost::iostreams::filtering_istream is;
                    OpenStream(file, m_gzipped, isfasta, is);

                    if(!m_usepairedends) {
                        while(NextRead(acc1, read1, isfasta, is, file))
                            InsertRead(read1, all_reads[1], file);
                    } else {
                        boost::regex re1("(.+)[./]1");
                        boost::cmatch matches1;
                        boost::regex re2("(.+)[./]2");
                        boost::cmatch matches2;
                        if(NextRead(acc1, read1, isfasta, is, file)) {
                            while(NextRead(acc2, read2, isfasta, is, file)) {
                                if(MatchIds(acc1, acc2)) {
                                    InsertRead(read1, all_reads[0], file);
                                    InsertRead(read2, all_reads[0], file);
                                    NextRead(acc1, read1, isfasta, is, file);
                                } else {
                                    InsertRead(read1, all_reads[1], file);
                                    acc1 = acc2;
                                    read1 = read2;
                                }
                            }
                            if(!read1.empty())
                                InsertRead(read1, all_reads[1], file);
                        }
                    }
                } else {
                    boost::iostreams::filtering_istream is1;
                    string file1 = file.substr(0,comma);
                    OpenStream(file1, m_gzipped, isfasta, is1);
                    boost::iostreams::filtering_istream is2;
                    string file2 = file.substr(comma+1);
                    OpenStream(file2, m_gzipped, isfasta, is2);
                    int p = m_usepairedends ? 0 : 1;
                    while(NextRead(acc1, read1, isfasta, is1, file1)) {
                        if(NextRead(acc2, read2, isfasta, is2, file2)) {
                            InsertRead(read1, all_reads[p], file1);
                            InsertRead(read2, all_reads[p], file2);
                        } else {
                            if(m_usepairedends)
                                throw runtime_error("Files "+file+" contain different number of mates");
                            else
                                InsertRead(read1, all_reads[p], file1);
                        }
                    }
                }
                if(total == all_reads[0].ReadNum()+all_reads[1].ReadNum())
                    throw runtime_error("File(s) "+file+" doesn't contain valid reads");
            }

            // divide reads into ncores chunks for multithreading
            size_t job_length = (all_reads[0].ReadNum()+all_reads[1].ReadNum())/m_ncores+1;
            job_length += job_length%2;
            size_t num = 0;
            for(CReadHolder::string_iterator is = all_reads[0].sbegin(); is != all_reads[0].send(); ++is, ++num) {
                if(num%job_length == 0 || m_reads.empty())
                    m_reads.push_back(array<CReadHolder,2>({CReadHolder(true), CReadHolder(false)}));
                m_reads.back()[0].PushBack(is);
            }
            for(CReadHolder::string_iterator is = all_reads[1].sbegin(); is != all_reads[1].send(); ++is, ++num) {
                if(num%job_length == 0 || m_reads.empty())
                    m_reads.push_back(array<CReadHolder,2>({CReadHolder(true), CReadHolder(false)}));
                m_reads.back()[1].PushBack(is);
            }
        }

        int m_ncores;
        bool m_usepairedends;
        bool m_gzipped;
        list<array<CReadHolder,2>> m_reads;
    };

}; // namespace
#endif /* _ReadsGetter_ */
