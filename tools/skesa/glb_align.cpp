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


#include "glb_align.hpp"
#include <sstream>
#include <limits>
#include <cmath>

using namespace std;
namespace DeBruijn {


void CCigar::PushFront(const SElement& el) {
    if(el.m_type == 'M') {
        m_qfrom -= el.m_len;
        m_sfrom -= el.m_len;
    } else if(el.m_type == 'D')
        m_sfrom -= el.m_len;
    else
        m_qfrom -= el.m_len;
            
    if(m_elements.empty() || m_elements.front().m_type != el.m_type)
        m_elements.push_front(el);
    else
        m_elements.front().m_len += el.m_len;
}

void CCigar::PushBack(const SElement& el) {
    if(el.m_type == 'M') {
        m_qto += el.m_len;
        m_sto += el.m_len;
    } else if(el.m_type == 'D')
        m_sto += el.m_len;
    else
        m_qto += el.m_len;
            
    if(m_elements.empty() || m_elements.back().m_type != el.m_type)
        m_elements.push_back(el);
    else
        m_elements.back().m_len += el.m_len;
}

string CCigar::CigarString(int qstart, int qlen) const {
    string cigar;
    for(auto& element :  m_elements)
        cigar += to_string(element.m_len)+element.m_type;

    int missingstart = qstart+m_qfrom;
    if(missingstart > 0)
        cigar = to_string(missingstart)+"S"+cigar;
    int missingend = qlen-1-m_qto-qstart;
    if(missingend > 0)
        cigar += to_string(missingend)+"S";

    return cigar;
}

string CCigar::DetailedCigarString(int qstart, int qlen, const  char* query, const  char* subject) const {
    string cigar;
    query += m_qfrom;
    subject += m_sfrom;
    for(auto& element : m_elements) {
        if(element.m_type == 'M') {
            bool is_match = *query == *subject;
            int len = 0;
            for(int l = 0; l < element.m_len; ++l) {
                if((*query == *subject) == is_match) {
                    ++len;
                } else {
                    cigar += to_string(len)+ (is_match ? "=" : "X"); 
                    is_match = !is_match;
                    len = 1;
                }
                ++query;
                ++subject;
            }
            cigar += to_string(len)+ (is_match ? "=" : "X"); 
        } else if(element.m_type == 'D') {
            cigar += to_string(element.m_len)+element.m_type;
            subject += element.m_len;
        } else {
            cigar += to_string(element.m_len)+element.m_type;
            query += element.m_len;
        }
    }

    int missingstart = qstart+m_qfrom;
    if(missingstart > 0)
        cigar = to_string(missingstart)+"S"+cigar;
    int missingend = qlen-1-m_qto-qstart;
    if(missingend > 0)
        cigar += to_string(missingend)+"S";
    
    return cigar;
}

TCharAlign CCigar::ToAlign(const  char* query, const  char* subject) const {
    TCharAlign align;
    query += m_qfrom;
    subject += m_sfrom;
    for(auto& element : m_elements) {
        if(element.m_type == 'M') {
            align.first.insert(align.first.end(), query, query+element.m_len);
            query += element.m_len;
            align.second.insert(align.second.end(), subject, subject+element.m_len);
            subject += element.m_len;
        } else if(element.m_type == 'D') {
            align.first.insert(align.first.end(), element.m_len, '-');
            align.second.insert(align.second.end(), subject, subject+element.m_len);
            subject += element.m_len;
        } else {
            align.first.insert(align.first.end(), query, query+element.m_len);
            query += element.m_len;
            align.second.insert(align.second.end(), element.m_len, '-');
        }
    }

    return align;
}

int CCigar::Matches(const  char* query, const  char* subject) const {
    int matches = 0;
    query += m_qfrom;
    subject += m_sfrom;
    for(auto& element : m_elements) {
        if(element.m_type == 'M') {
            for(int l = 0; l < element.m_len; ++l) {
                if(*query == *subject)
                    ++matches;
                ++query;
                ++subject;
            }
        } else if(element.m_type == 'D') {
            subject += element.m_len;
        } else {
            query += element.m_len;
        }
    }

    return matches;
}

int CCigar::Distance(const  char* query, const  char* subject) const {
    int dist = 0;
    query += m_qfrom;
    subject += m_sfrom;
    for(auto& element : m_elements) {
        if(element.m_type == 'M') {
            for(int l = 0; l < element.m_len; ++l) {
                if(*query != *subject)
                    ++dist;
                ++query;
                ++subject;
            }
        } else if(element.m_type == 'D') {
            subject += element.m_len;
            dist += element.m_len;
        } else {
            query += element.m_len;
            dist += element.m_len;
        }
    }

    return dist;
}

int CCigar::Score(const  char* query, const  char* subject, int gopen, int gapextend, const char delta[256][256]) const {
    int score = 0;

    query += m_qfrom;
    subject += m_sfrom;
    for(auto& element : m_elements) {
        if(element.m_type == 'M') {
            for(int l = 0; l < element.m_len; ++l) {
                score += delta[(int)*query][(int)*subject];
                ++query;
                ++subject;
            }
        } else if(element.m_type == 'D') {
            subject += element.m_len;
            score -= gopen+gapextend*element.m_len;
        } else {
            query += element.m_len;
            score -= gopen+gapextend*element.m_len;
        }
    }

    return score;
}

enum{Agap = 1, Bgap = 2, Astart = 4, Bstart = 8, Zero = 16};

CCigar BackTrack(int ia, int ib, char* m, int nb) {
    CCigar track(ia, ib);
    while((ia >= 0 || ib >= 0) && !(*m&Zero)) {
        if(*m&Agap) {
            int len = 1;
            while(!(*m&Astart)) {
                ++len;
                --m;
            }
            --m;
            ib -= len;
            track.PushFront(CCigar::SElement(len,'D'));
        } else if(*m&Bgap) {
            int len = 1;
            while(!(*m&Bstart)) {
                ++len;
                m -= nb+1;
            }
            m -= nb+1;
            ia -= len;
            track.PushFront(CCigar::SElement(len,'I'));
        } else {
            track.PushFront(CCigar::SElement(1,'M'));
            --ia;
            --ib;
            m -= nb+2;
        }
    }

    return track;
}

class CScore {
public:
    // we keep score and tiebreaker in int64 integer
    //!!!!!!!! tiebreaker must be >= 0 or it will spill into the score part !!!!!!!!!!
    CScore() : m_score(0) {}
    CScore(int32_t score, int32_t breaker) : m_score((int64_t(score) << 32) + breaker) {}
    bool operator>(const CScore& other) const { return m_score > other.m_score; }
    CScore  operator+(const CScore& other) const { 
        return CScore(m_score+other.m_score); 
    }
    CScore& operator+=(const CScore& other) {
        m_score += other.m_score;
        return *this;
    }
    int32_t Score() const { return (m_score >> 32); }
    
private:
    CScore(int64_t score) : m_score(score) {}
    int64_t m_score;
};

struct SRawMemory {
    SRawMemory(int na, int nb) {
        s = new CScore[nb+1];
        sm = new CScore[nb+1];
        gapb = new CScore[nb+1];
        mtrx = new  char[(na+1)*(nb+1)];
    }
    ~SRawMemory() {
        delete[] s;
        delete[] sm;
        delete[] gapb;
        delete[] mtrx;
    }

	CScore* s;          // best scores in current a-raw
	CScore* sm;         // best scores in previous a-raw
	CScore* gapb;       // best score with b-gap
    char* mtrx;         // backtracking info (Astart/Bstart gap start, Agap/Bgap best score has gap and should be backtracked to Asrt/Bsart; Zero stop bactracking)
};

CCigar GlbAlign(const  char* a, int na, const  char*  b, int nb, int rho, int sigma, const char delta[256][256]) {
    //	rho - new gap penalty (one base gap rho+sigma)
    // sigma - extension penalty

    SRawMemory memory(na, nb);
	CScore* s = memory.s;       // best scores in current a-raw
	CScore* sm = memory.sm;     // best scores in previous a-raw
	CScore* gapb = memory.gapb; // best score with b-gap
    char* mtrx = memory.mtrx;   // backtracking info (Astart/Bstart gap start, Agap/Bgap best score has gap and should be backtracked to Asrt/Bsart; Zero stop bactracking)

    CScore rsa(-rho-sigma, 0);   // new gapa
    CScore rsb(-rho-sigma, 1);   // new gapb  
    CScore bignegative(numeric_limits<int>::min()/2, 0);

    sm[0] = CScore();
	sm[1] = rsa;                  // scores for --------------   (the best scores for i == -1)
	for(int i = 2; i <= nb; ++i)  //            BBBBBBBBBBBBBB
        sm[i] = sm[i-1]+CScore(-sigma, 0);    
	s[0] = rsb;                   // score for A   (the best score for j == -1 and i == 0)
                                  //           -
	for(int i = 0; i <= nb; ++i) 
        gapb[i] = bignegative;
	
    mtrx[0] = 0;
    for(int i = 1; i <= nb; ++i) {  // ---------------
        mtrx[i] = Agap;             // BBBBBBBBBBBBBBB
    }
    mtrx[1] |= Astart;
	
    char* m = mtrx+nb;
	for(int i = 0; i < na; ++i) {
		*(++m) = Bstart|Bgap;       //AAAAAAAAAAAAAAA
                                    //---------------
        CScore gapa = bignegative;
		int ai = a[i];
        const char* matrix = delta[ai];
		CScore* sp = s;
		for(int j = 0; j < nb; ) {
			*(++m) = 0;
			CScore ss = sm[j]+CScore(matrix[(int)b[j]], 1);  // diagonal extension

            gapa += CScore(-sigma, 0);  // gapa extension
			if(*sp+rsa > gapa) {        // for j == 0 this will open   AAAAAAAAAAA-  which could be used if mismatch is very expensive
				gapa = *sp+rsa;         //                             -----------B
				*m |= Astart;
			}
			
			CScore& gapbj = gapb[++j];
			gapbj += CScore(-sigma, 1); // gapb extension
			if(sm[j]+rsb > gapbj) {     // for i == 0 this will open  BBBBBBBBBBB- which could be used if mismatch is very expensive
				gapbj = sm[j]+rsb;      //                            -----------A
				*m |= Bstart;
			}
			 
			if(gapa > gapbj) {
				if(ss > gapa) {
					*(++sp) = ss;
				} else {
					*(++sp) = gapa;
					*m |= Agap;
				}
			} else {
				if(ss > gapbj) {
					*(++sp) = ss;
				} else {
					*(++sp) = gapbj;
					*m |= Bgap;
				}
			}
		}
		swap(sm,s);
		*s = *sm+CScore(-sigma, 1); 
	}
	
	int ia = na-1;
	int ib = nb-1;
	m = mtrx+(na+1)*(nb+1)-1;
    return BackTrack(ia, ib, m, nb);
}

CCigar LclAlign(const  char* a, int na, const  char*  b, int nb, int rho, int sigma, const char delta[256][256]) {
    //	rho - new gap penalty (one base gap rho+sigma)
    // sigma - extension penalty

    SRawMemory memory(na, nb);
	CScore* s = memory.s;       // best scores in current a-raw
	CScore* sm = memory.sm;     // best scores in previous a-raw
	CScore* gapb = memory.gapb; // best score with b-gap
    char* mtrx = memory.mtrx;   // backtracking info (Astart/Bstart gap start, Agap/Bgap best score has gap and should be backtracked to Asrt/Bsart; Zero stop bactracking)

    CScore rsa(-rho-sigma, 0);   // new gapa
    CScore rsb(-rho-sigma, 1);   // new gapb  

    for(int i = 0; i <= nb; ++i) {
        sm[i] = CScore();
        mtrx[i] = Zero;
        gapb[i] = CScore();
    }
    s[0] = CScore();

    CScore max_score;
    char* max_ptr = mtrx;
    char* m = mtrx+nb;
	
    for(int i = 0; i < na; ++i) {
		*(++m) = Zero;
		CScore gapa;
		int ai = a[i];
        const char* matrix = delta[ai];
		CScore* sp = s;
		for(int j = 0; j < nb; ) {
			*(++m) = 0;
			CScore ss = sm[j]+CScore(matrix[(int)b[j]], 1);  // diagonal extension

            gapa += CScore(-sigma, 0);  // gapa extension
			if(*sp+rsa > gapa) {
				gapa = *sp+rsa;         // new gapa
				*m |= Astart;
			}
			
			CScore& gapbj = gapb[++j];
			gapbj += CScore(-sigma, 1); // gapb extension
			if(sm[j]+rsb > gapbj) {
				gapbj = sm[j]+rsb;      // new gapb
				*m |= Bstart;
			}
			 
			if(gapa > gapbj) {
				if(ss > gapa) {
					*(++sp) = ss;
                    if(ss > max_score) {
                        max_score = ss;
                        max_ptr = m;
                    }
				} else {
					*(++sp) = gapa;
					*m |= Agap;
				}
			} else {
				if(ss > gapbj) {
					*(++sp) = ss;
                    if(ss > max_score) {
                        max_score = ss;
                        max_ptr = m;
                    }
				} else {
					*(++sp) = gapbj;
					*m |= Bgap;
				}
			}
            if(sp->Score() <= 0) {
                *sp = CScore();
                *m |= Zero;  
            }
		}
		swap(sm,s);
	}

    int ia = (max_ptr-mtrx)/(nb+1)-1;
    int ib = (max_ptr-mtrx)%(nb+1)-1;
    m = max_ptr;
    return BackTrack(ia, ib, m, nb);
}


CCigar LclAlign(const  char* a, int na, const  char*  b, int nb, int rho, int sigma, bool pinleft, bool pinright, const char delta[256][256]) {
    //	rho - new gap penalty (one base gap rho+sigma)
    // sigma - extension penalty

    SRawMemory memory(na, nb);
	CScore* s = memory.s;       // best scores in current a-raw
	CScore* sm = memory.sm;     // best scores in previous a-raw
	CScore* gapb = memory.gapb; // best score with b-gap
    char* mtrx = memory.mtrx;   // backtracking info (Astart/Bstart gap start, Agap/Bgap best score has gap and should be backtracked to Asrt/Bsart; Zero stop bactracking)

    CScore rsa(-rho-sigma, 0);   // new gapa
    CScore rsb(-rho-sigma, 1);   // new gapb  
    CScore bignegative(numeric_limits<int>::min()/2, 0);
    
    sm[0] = CScore();
    mtrx[0] = 0;
    gapb[0] = bignegative;  // not used
    if(pinleft) {
        if(nb > 0) {
            sm[1] = rsa;
            mtrx[1] = Astart|Agap;
            gapb[1] = bignegative;
            for(int i = 2; i <= nb; ++i) {
                sm[i] = sm[i-1]+CScore(-sigma, 0);
                mtrx[i] = Agap;
                gapb[i] = bignegative;
            }
        }
        s[0] = rsb; 
    } else {
        for(int i = 1; i <= nb; ++i) {
            sm[i] = CScore();
            mtrx[i] = Zero;
            gapb[i] = bignegative;
        }
        s[0] = CScore();
    }

    CScore max_score;
    char* max_ptr = mtrx;
	
    char* m = mtrx+nb;
	for(int i = 0; i < na; ++i) {
		*(++m) = pinleft ? Bstart|Bgap : Zero;
		CScore gapa = bignegative;
		int ai = a[i];
		
		const char* matrix = delta[ai];
		CScore* sp = s;
		for(int j = 0; j < nb; ) {
			*(++m) = 0;
			CScore ss = sm[j]+CScore(matrix[(int)b[j]], 1);  // diagonal extension

            gapa += CScore(-sigma, 0);  // gapa extension
			if(*sp+rsa > gapa) {
				gapa = *sp+rsa;         // new gapa
				*m |= Astart;
			}
			
			CScore& gapbj = gapb[++j];
			gapbj += CScore(-sigma, 1); // gapb extension
			if(sm[j]+rsb > gapbj) {
				gapbj = sm[j]+rsb;      // new gapb
				*m |= Bstart;
			}
			 
			if(gapa > gapbj) {
				if(ss > gapa) {
					*(++sp) = ss;
                    if(ss > max_score) {
                        max_score = ss;
                        max_ptr = m;
                    }
				} else {
					*(++sp) = gapa;
					*m |= Agap;
				}
			} else {
				if(ss > gapbj) {
					*(++sp) = ss;
                    if(ss > max_score) {
                        max_score = ss;
                        max_ptr = m;
                    }
				} else {
					*(++sp) = gapbj;
					*m |= Bgap;
				}
			}
            if(sp->Score() <= 0 && !pinleft) {
                *sp = CScore();
                *m |= Zero;  
            }
		}
		swap(sm,s);
        if(pinleft)
            *s = *sm+CScore(-sigma, 1); 
	}

    int maxa, maxb;
    if(pinright) {
        maxa = na-1;
        maxb = nb-1;
        max_score = sm[nb];
    } else {
        maxa = (max_ptr-mtrx)/(nb+1)-1;
        maxb = (max_ptr-mtrx)%(nb+1)-1;
        m = max_ptr;
    }
    int ia = maxa;
    int ib = maxb;
    return BackTrack(ia, ib, m, nb);
}

CCigar VariBandAlign(const  char* a, int na, const  char*  b, int nb, int rho, int sigma, const char delta[256][256], const TRange* blimits) {
    //	rho - new gap penalty (one base gap rho+sigma)
    // sigma - extension penalty

    SRawMemory memory(na, nb);
	CScore* s = memory.s;       // best scores in current a-raw
	CScore* sm = memory.sm;     // best scores in previous a-raw
	CScore* gapb = memory.gapb; // best score with b-gap
    char* mtrx = memory.mtrx;   // backtracking info (Astart/Bstart gap start, Agap/Bgap best score has gap and should be backtracked to Asrt/Bsart; Zero stop bactracking)

    CScore rsa(-rho-sigma, 0);   // new gapa
    CScore rsb(-rho-sigma, 1);   // new gapb  

    for(int i = 0; i <= nb; ++i) {
        s[i] = CScore();
        sm[i] = CScore();
        gapb[i] = CScore();
        mtrx[i] = Zero;
    }

    CScore max_score;
    char* max_ptr = mtrx;
    char* m = mtrx+nb;
	
    const TRange* last = blimits+na;
    while(true) {
		int ai = *a++;
		const char* matrix = delta[ai];

        int bleft = blimits->first;
        int bright = blimits->second;
        m += bleft;
        *(++m) = Zero;
		CScore gapa;
        CScore* sp = s+bleft;
        *sp = CScore();
		for(int j = bleft; j <= bright; ) {
			*(++m) = 0;
			CScore ss = sm[j]+CScore(matrix[(int)b[j]], 1);  // diagonal extension

            gapa += CScore(-sigma, 0);  // gapa extension
			if(*sp+rsa > gapa) {
				gapa = *sp+rsa;
				*m |= Astart;
			}
			
			CScore& gapbj = gapb[++j];
			gapbj += CScore(-sigma, 1); // gapb extension
			if(sm[j]+rsb > gapbj) {
				gapbj = sm[j]+rsb;
				*m |= Bstart;
			}
			 
			if(gapa > gapbj) {
				if(ss > gapa) {
					*(++sp) = ss;
                    if(ss > max_score) {
                        max_score = ss;
                        max_ptr = m;
                    }
				} else {
					*(++sp) = gapa;
					*m |= Agap;
				}
			} else {
				if(ss > gapbj) {
					*(++sp) = ss;
                    if(ss > max_score) {
                        max_score = ss;
                        max_ptr = m;
                    }
				} else {
					*(++sp) = gapbj;
					*m |= Bgap;
				}
			}
            if(sp->Score() <= 0) {
                *sp = CScore();
                *m |= Zero;  
            }
		}
        if(++blimits == last)
            break;


		swap(sm,s);
        m -= bright+1; // beginning of the current raw

        //clean up (s - self sustained)
        int nextr = blimits->second;
        //right increased
        for(int l = bright+1; l <= nextr; ++l)
            m[l+1] = Zero;
        //right decreased
        for(int l = nextr+1; l <= bright; ++l) {
            gapb[l+1] = CScore();
            sm[l+1] = CScore();
        }
        int nextl = blimits->first;
        //left decreased
        for(int l = nextl-1; l <= bleft-1; ++l) {
            gapb[l+1] = CScore();
            sm[l+1] = CScore();
            m[l+1] = Zero;
        }

        m += nb;     // end of the current raw
	}
  
    int ia = (max_ptr-mtrx)/(nb+1)-1;
    int ib = (max_ptr-mtrx)%(nb+1)-1;
    m = max_ptr;
    return BackTrack(ia, ib, m, nb);
}


SMatrix::SMatrix(int match, int mismatch) { // matrix for DNA

    for(int i = 0; i < 256;  ++i) {
        int c = toupper(i);
        for(int j = 0; j < 256;  ++j) {
            if(c != 'N' && c == toupper(j)) matrix[i][j] = match;
            else matrix[i][j] = -mismatch;
        }
    }
}
	
SMatrix::SMatrix() { // matrix for proteins   

    string aa("ARNDCQEGHILKMFPSTWYVBZX*");
    int scores[] = {
 4,-1,-2,-2, 0,-1,-1, 0,-2,-1,-1,-1,-1,-2,-1, 1, 0,-3,-2, 0,-2,-1, 0,-4,
-1, 5, 0,-2,-3, 1, 0,-2, 0,-3,-2, 2,-1,-3,-2,-1,-1,-3,-2,-3,-1, 0,-1,-4,
-2, 0, 6, 1,-3, 0, 0, 0, 1,-3,-3, 0,-2,-3,-2, 1, 0,-4,-2,-3, 3, 0,-1,-4,
-2,-2, 1, 6,-3, 0, 2,-1,-1,-3,-4,-1,-3,-3,-1, 0,-1,-4,-3,-3, 4, 1,-1,-4,
 0,-3,-3,-3, 9,-3,-4,-3,-3,-1,-1,-3,-1,-2,-3,-1,-1,-2,-2,-1,-3,-3,-2,-4,
-1, 1, 0, 0,-3, 5, 2,-2, 0,-3,-2, 1, 0,-3,-1, 0,-1,-2,-1,-2, 0, 3,-1,-4,
-1, 0, 0, 2,-4, 2, 5,-2, 0,-3,-3, 1,-2,-3,-1, 0,-1,-3,-2,-2, 1, 4,-1,-4,
 0,-2, 0,-1,-3,-2,-2, 6,-2,-4,-4,-2,-3,-3,-2, 0,-2,-2,-3,-3,-1,-2,-1,-4,
-2, 0, 1,-1,-3, 0, 0,-2, 8,-3,-3,-1,-2,-1,-2,-1,-2,-2, 2,-3, 0, 0,-1,-4,
-1,-3,-3,-3,-1,-3,-3,-4,-3, 4, 2,-3, 1, 0,-3,-2,-1,-3,-1, 3,-3,-3,-1,-4,
-1,-2,-3,-4,-1,-2,-3,-4,-3, 2, 4,-2, 2, 0,-3,-2,-1,-2,-1, 1,-4,-3,-1,-4,
-1, 2, 0,-1,-3, 1, 1,-2,-1,-3,-2, 5,-1,-3,-1, 0,-1,-3,-2,-2, 0, 1,-1,-4,
-1,-1,-2,-3,-1, 0,-2,-3,-2, 1, 2,-1, 5, 0,-2,-1,-1,-1,-1, 1,-3,-1,-1,-4,
-2,-3,-3,-3,-2,-3,-3,-3,-1, 0, 0,-3, 0, 6,-4,-2,-2, 1, 3,-1,-3,-3,-1,-4,
-1,-2,-2,-1,-3,-1,-1,-2,-2,-3,-3,-1,-2,-4, 7,-1,-1,-4,-3,-2,-2,-1,-2,-4,
 1,-1, 1, 0,-1, 0, 0, 0,-1,-2,-2, 0,-1,-2,-1, 4, 1,-3,-2,-2, 0, 0, 0,-4,
 0,-1, 0,-1,-1,-1,-1,-2,-2,-1,-1,-1,-1,-2,-1, 1, 5,-2,-2, 0,-1,-1, 0,-4,
-3,-3,-4,-4,-2,-2,-3,-2,-2,-3,-2,-3,-1, 1,-4,-3,-2,11, 2,-3,-4,-3,-2,-4,
-2,-2,-2,-3,-2,-1,-2,-3, 2,-1,-1,-2,-1, 3,-3,-2,-2, 2, 7,-1,-3,-2,-1,-4,
 0,-3,-3,-3,-1,-2,-2,-3,-3, 3, 1,-2, 1,-1,-2,-2, 0,-3,-1, 4,-3,-2,-1,-4,
-2,-1, 3, 4,-3, 0, 1,-1, 0,-3,-4, 0,-3,-3,-2, 0,-1,-4,-3,-3, 4, 1,-1,-4,
-1, 0, 0, 1,-3, 3, 4,-2, 0,-3,-3, 1,-1,-3,-1, 0,-1,-3,-2,-2, 1, 4,-1,-4,
 0,-1,-1,-1,-2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-2, 0, 0,-2,-1,-1,-1,-1,-1,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4, 1
    };

    for(int i = 0; i < 256;  ++i) {
        for(int j = 0; j < 256;  ++j) {
            matrix[i][j] = 0;
        }
    }

    int num = aa.size();
    for(int i = 0; i < num; ++i) {
        char c = aa[i];
        for(int j = 0; j < num; ++j) {
            int score = scores[num*j+i];
            char d = aa[j];
            matrix[(int)c][(int)d] = score;
            matrix[(int)tolower(c)][(int)tolower(d)] = score;
            matrix[(int)c][(int)tolower(d)] = score;
            matrix[(int)tolower(c)][(int)d] = score;
        }
    }
}

double Entropy(const string& seq) {
    int length = seq.size();
    if(length == 0)
        return 0;
    double tA = 1.e-8;
    double tC = 1.e-8;
    double tG = 1.e-8;
    double tT = 1.e-8;
    for(char c : seq) {
        switch(c) {
        case 'A': tA += 1; break;
        case 'C': tC += 1; break;
        case 'G': tG += 1; break;
        case 'T': tT += 1; break;
        default: break;
        }
    }
    double entropy = -(tA*log(tA/length)+tC*log(tC/length)+tG*log(tG/length)+tT*log(tT/length))/(length*log(4.));
    
    return entropy;
}

}; // namespace
	



