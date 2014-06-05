/***************************************************************************
  *  Copyright S. V. Paulauskas 2014                                       *
  *                                                                        *
  *  This program is free software: you can redistribute it and/or modify  *
  *  it under the terms of the GNU General Public License as published by  *
  *  the Free Software Foundation, version 3.0 License.                    *
  *                                                                        *
  *  This program is distributed in the hope that it will be useful,       *
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of        *
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
  *  GNU General Public License for more details.                          *
  *                                                                        *
  *  You should have received a copy of the GNU General Public License     *
  *  along with this program.  If not, see <http://www.gnu.org/licenses/>. *
  **************************************************************************
*/

/** \file TraceFilter.cpp
 *  \brief A class to perform trapezoidal filtering
 *  \author S. V. Paulauskas
 *  \date 23 April 2014
 *
 *  Copyright S. V. Paulauskas 2014
 *
 *  This code is based off of the IGOR macro energy.ipf
 *  written by H. Tan of XIA LLC and parts of the nscope
 *  program written at the NSC written by C.Prokop.
 *
 *  Published under the GNU GPL v. 3.0
 *
 */
#include <algorithm>
#include <iostream>

#include <cmath>

#include "TraceFilter.hpp"

using namespace std;

//----------- Filter Parameter Methods ----------
FilterParameters::FilterParameters(const double &l, const double &g,
    const double &tau) {
    l_ = l;
    g_ = g;
    tau_ = tau;
}

FilterParameters::FilterParameters(const double &l, const double &g,
                                   const unsigned int &thresh) {
    l_ = l;
    g_ = g;
    thresh_ = thresh;
}

//----------- Trace Filter Methods -----------
TraceFilter::TraceFilter(const unsigned int &adc,
                         const FilterParameters &tFilt,
                         const FilterParameters &eFilt) {
    e_ = eFilt;
    t_ = tFilt;
    adc_ = adc;
    loud_ = false;
}

void TraceFilter::CalcBaseline(void) {
    //We need at least 2*l+g (energy filter pars) to calculate the baseline
    double l = e_.GetRisetime(), g = e_.GetFlattop();
    unsigned int offset = trigPos_ - l - 10;
    unsigned int numbases = floor(offset/(2*l+g));
    baseline_ = 0;

    if(numbases == 0) {
        cerr << "There are not enough bins for the baseline! I will be recording"
             << "an energy of Zero (O) for this event!! Expect problems! " 
             << endl;
        baseline_ = 0;
        return;
    }

    double partA, partB, partC;
    for(unsigned int k = 0; k < numbases; k++) {
        unsigned int offnew = offset - (2*l+g)*(k+1);
        for(unsigned int i = offnew; i < offnew+l-1; i++)
            partA += sig_->at(i);
        for(unsigned int i = offnew+l; i < offnew+l+g-1; i++)
            partB += sig_->at(i);
        for(unsigned int i = offnew+l+g; i < offnew+2*l+g-1; i++)
            partC += sig_->at(i);
        baseline_ += coeffs_[0]*partA + coeffs_[1]*partB + coeffs_[2]*partC;
    }
    baseline_ /= numbases;

    // for(unsigned int i = 0; i < 15; i++)
    //     baseline_ += sig_->at(i);
    // baseline_ /= 15;
}

void TraceFilter::CalcFilters(const vector<double> *sig) {
    sig_ = sig;

    if(!finishedConvert_)
        ConvertToClockticks();

    CalcTriggerFilter();
    CalcEnergyFilterLimits();
    CalcEnergyFilterCoeffs();
    
    CalcBaseline();

    if(baseline_ == 0)
        energy_ = 0;
    else
        CalcEnergyFilter();
}

void TraceFilter::CalcEnergyFilter(void) {
    energy_ = 0;
    double partA = 0, partB = 0, partC = 0;

    for(unsigned int i = limits_[0]; i < limits_[1]; i++)
        partA += sig_->at(i);
    for(unsigned int i = limits_[2]; i < limits_[3]; i++)
        partB += sig_->at(i);
    for(unsigned int i = limits_[4]; i < limits_[5]; i++)
        partC += sig_->at(i);

    energy_ = coeffs_[0]*partA + coeffs_[1]*partB + coeffs_[2]*partC;
    energy_ -= baseline_;
}

void TraceFilter::CalcEnergyFilterCoeffs(void) {
    double beta = exp(-1.0/e_.GetTau());
    double cg = 1-beta;
    double ctmp = 1-pow(beta,e_.GetRisetime());
    coeffs_.push_back(-(cg/ctmp)*pow(beta,e_.GetRisetime()));
    coeffs_.push_back(cg);
    coeffs_.push_back(cg/ctmp);

    if(loud_)
        cout << "The Energy Filter Coefficients: " << endl
             << "  CRise : " << coeffs_[0] << endl
             << "  CGap  : " << coeffs_[1] << endl
             << "  CFall : " << coeffs_[2] << endl;
}

void TraceFilter::CalcEnergyFilterLimits(void) {
    limits_.clear();
    double l = e_.GetRisetime(), g = e_.GetFlattop();

    double p0 = trigPos_ - l - 10;
    double p1 = p0+l-1;
    double p2 = p0+l;
    double p3 = p0+l+g-1;
    double p4 = p0+l+g;
    double p5 = p0+2*l+g-1;

    if(p5 > sig_->size())
        p5 = (double)sig_->size();

    if(loud_)
        cout << "The limits for the Energy filter sums: " << endl
             << "  Rise Sum : Low = " << p0 << " High = " << p1 << endl
             << "  Gap Sum  : Low = " << p2 << " High = " << p3 << endl
             << "  Fall Sum : Low = " << p4 << " High = " << p5 << endl;

    limits_.push_back(p0);      // beginning of  sum E0
    limits_.push_back(p1);      // end of sum E0
    limits_.push_back(p2);      // beginning of gap sum
    limits_.push_back(p3);      // end of gap sum
    limits_.push_back(p4);      // beginning of sum E1
    limits_.push_back(p5);      // end of sum E1
}

void TraceFilter::CalcTriggerFilter(void) {
    trigFilter_.clear();
    trigPos_ = 0;
    int l = t_.GetRisetime(), g = t_.GetFlattop();
    for(int i = 0; i < (int)sig_->size(); i++) {
        double sum1 = 0, sum2 = 0;
        if( (i-2*l-g+1) >= 0 ) {
            for(int a = i-2*l-g+1; a < i-l-g+1; a++)
                sum1 += sig_->at(a);
            for(int a = i-l+1; a < i+1; a++)
                sum2 += sig_->at(a);
            if((sum2 - sum1)/sig_->at(i) > t_.GetThreshold() && trigPos_ == 0)
                trigPos_ = i;
            trigFilter_.push_back(sum2 - sum1);
        }
    }
    if(loud_)
        cout << "The Trigger Position : " << endl << "  " << trigPos_ << endl;
}

void TraceFilter::ConvertToClockticks(void) {
    //we should make sure that everything is an integral number of clockticks
    if(fmod(t_.GetRisetime(),adc_) != 0) {
        if(loud_)
            cout << "TriggerRisetime is NOT an integer number of samples. Fixing."
                 << endl;
        t_.SetRisetime(ceil(t_.GetRisetime()*adc_));
    } else
        t_.SetRisetime(t_.GetRisetime() / adc_);

    if(t_.GetRisetime() !=0 && fmod(t_.GetFlattop(),adc_) != 0) {
        if(loud_)
            cout << "TriggerGap is NOT an integer numnber of samples. Fixing."
                 << endl;
        t_.SetFlattop(ceil(t_.GetFlattop()*adc_));
    } else
        t_.SetFlattop(t_.GetFlattop()*adc_);

    if(fmod(e_.GetRisetime(),adc_) != 0) {
        if(loud_)
            cout << "EnergyRisetime is NOT an integer number of samples.  Fixing."
                 << endl;
        e_.SetRisetime(ceil(e_.GetRisetime()*adc_));
    } else
        e_.SetRisetime(e_.GetRisetime()*adc_);

    if(e_.GetFlattop() != 0 && fmod(e_.GetFlattop(),adc_) != 0) {
        if(loud_)
            cout << "EnergyGap is NOT an integer numnber of samples. Fixing."
                 << endl;
        e_.SetFlattop(ceil(e_.GetFlattop()*adc_));
    } else
        e_.SetFlattop(e_.GetFlattop()*adc_);

    //Put tau in units of samples
    e_.SetTau(e_.GetTau()*adc_);

    if(loud_) {
        cout << "Here are the used filter parameters: " << endl
             << "  Fast rise (ns):   " << t_.GetRisetime()*1000/adc_ <<  endl
             << "  Fast flat (ns):   " << t_.GetFlattop()*1000/adc_ <<  endl
             << "  Thresh (ADC units) : " << t_.GetThreshold() << endl
             << "  Energy rise (ns): " << e_.GetRisetime()*1000/adc_ <<  endl
             << "  Energy flat (ns): " << e_.GetFlattop()*1000/adc_ <<  endl
             << "  Tau(ns) :         " << e_.GetTau()*1000/adc_ << endl;
    }
    finishedConvert_ = true;
}
