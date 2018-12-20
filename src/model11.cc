#include "model.h"

#include "ode.h"

inline long double safe_sum_log(long double a, long double b) {
  // The general algorithm
  //auto c = std::max(a, b);
  //return log(exp(a-c) + exp(b-c)) + c;
  
  // Optimised
  if (a > b)
    return log(1 + exp(b-a)) + a;
  else
    return log(exp(a-b) + 1) + b;
}

namespace flu 
{
    boost::posix_time::ptime getTimeFromWeekYear( int week, int year )
    {
        namespace bt = boost::posix_time;
        namespace bg = boost::gregorian;
        // Week 1 is the first week that ends in this year
        auto firstThursday = bg::first_day_of_the_week_in_month( 
                bg::Thursday, bg::Jan );
        auto dateThursday = firstThursday.get_date( year );
        auto current_time = bt::ptime(dateThursday) - bt::hours(24*3);
        current_time += bt::hours(24*7*(week-1)+12); // Return midday of monday
        return current_time;
    }

    enum seir_type_t { S = 0, E1 = 1, E2 = 2, I1 = 3, I2 = 4, R = 5 };
    const std::vector<seir_type_t> seir_types = 
        { S, E1, E2, I1, I2, R };

    enum group_type_t { LOW = 0, HIGH = 1, PREG = 2,
        VACC_LOW = 3, VACC_HIGH = 4, VACC_PREG = 5 };
    const std::vector<group_type_t> group_types = { LOW, HIGH, PREG,
        VACC_LOW, VACC_HIGH, VACC_PREG };

    inline size_t ode_id( const size_t nag, const group_type_t gt, 
            const seir_type_t st )
    {
        return gt*nag*seir_types.size()
        + st*nag;
    }

    inline size_t ode_id( const size_t nag, const group_type_t gt, 
            const seir_type_t st, 
            const size_t i )
    {
        return gt*nag*seir_types.size()
        + st*nag + i;
    }

    inline Eigen::VectorXd flu_ode( Eigen::VectorXd &deltas,
            const Eigen::VectorXd &densities,
            const Eigen::VectorXd &Npop,
            const Eigen::MatrixXd &vaccine_rates, // If empty, rate of zero is assumed
            const Eigen::VectorXd &vaccine_efficacy,
            const Eigen::MatrixXd &transmission_regular,
            double a1, double a2, double g1, double g2 )
    {
        const size_t nag = transmission_regular.cols();
        
        for(size_t i=0;i<nag;i++)
        {
            /*rate of depletion of susceptible*/
            deltas[ode_id(nag,VACC_LOW,S,i)]=0;
            for(size_t j=0;j<nag;j++)
                deltas[ode_id(nag,VACC_LOW,S,i)]+=transmission_regular(i,j)*(densities[ode_id(nag,VACC_LOW,I1,j)]+densities[ode_id(nag,VACC_LOW,I2,j)]+densities[ode_id(nag,VACC_HIGH,I1,j)]+densities[ode_id(nag,VACC_HIGH,I2,j)]+densities[ode_id(nag,VACC_PREG,I1,j)]+densities[ode_id(nag,VACC_PREG,I2,j)]+densities[ode_id(nag,LOW,I1,j)]+densities[ode_id(nag,LOW,I2,j)]+densities[ode_id(nag,HIGH,I1,j)]+densities[ode_id(nag,HIGH,I2,j)]+densities[ode_id(nag,PREG,I1,j)]+densities[ode_id(nag,PREG,I2,j)]);

            deltas[ode_id(nag,VACC_HIGH,S,i)]=deltas[ode_id(nag,VACC_LOW,S,i)];
            deltas[ode_id(nag,VACC_PREG,S,i)]=deltas[ode_id(nag,VACC_LOW,S,i)];
            deltas[ode_id(nag,LOW,S,i)]=deltas[ode_id(nag,VACC_LOW,S,i)];
            deltas[ode_id(nag,HIGH,S,i)]=deltas[ode_id(nag,VACC_LOW,S,i)];
            deltas[ode_id(nag,PREG,S,i)]=deltas[ode_id(nag,VACC_LOW,S,i)];

            deltas[ode_id(nag,VACC_LOW,S,i)]*=-densities[ode_id(nag,VACC_LOW,S,i)];
            deltas[ode_id(nag,VACC_HIGH,S,i)]*=-densities[ode_id(nag,VACC_HIGH,S,i)];
            deltas[ode_id(nag,VACC_PREG,S,i)]*=-densities[ode_id(nag,VACC_PREG,S,i)];
            deltas[ode_id(nag,LOW,S,i)]*=-densities[ode_id(nag,LOW,S,i)];
            deltas[ode_id(nag,HIGH,S,i)]*=-densities[ode_id(nag,HIGH,S,i)];
            deltas[ode_id(nag,PREG,S,i)]*=-densities[ode_id(nag,PREG,S,i)];
        }

        /*rate of passing between states of infection*/
        for ( auto &gt : group_types)
        {
            deltas.segment(ode_id(nag,gt,E1),nag)=-deltas.segment(ode_id(nag,gt,S),nag)-a1*densities.segment(ode_id(nag,gt,E1),nag);
            deltas.segment(ode_id(nag,gt,E2),nag)=a1*densities.segment(ode_id(nag,gt,E1),nag)-a2*densities.segment(ode_id(nag,gt,E2),nag);

            deltas.segment(ode_id(nag,gt,I1),nag)=a2*densities.segment(ode_id(nag,gt,E2),nag)-g1*densities.segment(ode_id(nag,gt,I1),nag);
            deltas.segment(ode_id(nag,gt,I2),nag)=g1*densities.segment(ode_id(nag,gt,I1),nag)-g2*densities.segment(ode_id(nag,gt,I2),nag);
            deltas.segment(ode_id(nag,gt,R),nag)=g2*densities.segment(ode_id(nag,gt,I2),nag);
        }

        /*Vaccine bit*/

        if ( vaccine_rates.size() > 0 )
        {
            for(size_t i=0;i<nag;i++)
            {
                double vacc_prov = 0;
                if (Npop[i]>0) // If zero then densities also zero -> 0/0
                    vacc_prov=Npop[i]*vaccine_rates(i)/(densities[ode_id(nag,LOW,S,i)]+densities[ode_id(nag,LOW,E1,i)]+densities[ode_id(nag,LOW,E2,i)]+densities[ode_id(nag,LOW,I1,i)]+densities[ode_id(nag,LOW,I2,i)]+densities[ode_id(nag,LOW,R,i)]);
                double vacc_prov_r = 0;
                if (Npop[i+nag]>0)
                    vacc_prov_r=Npop[i+nag]*vaccine_rates(i+nag)/(densities[ode_id(nag,HIGH,S,i)]+densities[ode_id(nag,HIGH,E1,i)]+densities[ode_id(nag,HIGH,E2,i)]+densities[ode_id(nag,HIGH,I1,i)]+densities[ode_id(nag,HIGH,I2,i)]+densities[ode_id(nag,HIGH,R,i)]);
                double vacc_prov_p = 0;
                if (Npop[i+2*nag]>0)
                    vacc_prov_p=Npop[i+2*nag]*vaccine_rates(i+2*nag)/(densities[ode_id(nag,PREG,S,i)]+densities[ode_id(nag,PREG,E1,i)]+densities[ode_id(nag,PREG,E2,i)]+densities[ode_id(nag,PREG,I1,i)]+densities[ode_id(nag,PREG,I2,i)]+densities[ode_id(nag,PREG,R,i)]);

                deltas[ode_id(nag,VACC_LOW,S,i)]+=densities[ode_id(nag,LOW,S,i)]*vacc_prov*(1-vaccine_efficacy[nag*LOW+i]);
                deltas[ode_id(nag,VACC_HIGH,S,i)]+=densities[ode_id(nag,HIGH,S,i)]*vacc_prov_r*(1-vaccine_efficacy[nag*HIGH+i]);
                deltas[ode_id(nag,VACC_PREG,S,i)]+=densities[ode_id(nag,PREG,S,i)]*vacc_prov_p*(1-vaccine_efficacy[nag*PREG+i]);
                deltas[ode_id(nag,LOW,S,i)]-=densities[ode_id(nag,LOW,S,i)]*vacc_prov;
                deltas[ode_id(nag,HIGH,S,i)]-=densities[ode_id(nag,HIGH,S,i)]*vacc_prov_r;
                deltas[ode_id(nag,PREG,S,i)]-=densities[ode_id(nag,PREG,S,i)]*vacc_prov_p;

                deltas[ode_id(nag,VACC_LOW,E1,i)]+=densities[ode_id(nag,LOW,E1,i)]*vacc_prov;
                deltas[ode_id(nag,VACC_HIGH,E1,i)]+=densities[ode_id(nag,HIGH,E1,i)]*vacc_prov_r;
                deltas[ode_id(nag,VACC_PREG,E1,i)]+=densities[ode_id(nag,PREG,E1,i)]*vacc_prov_p;
                deltas[ode_id(nag,LOW,E1,i)]-=densities[ode_id(nag,LOW,E1,i)]*vacc_prov;
                deltas[ode_id(nag,HIGH,E1,i)]-=densities[ode_id(nag,HIGH,E1,i)]*vacc_prov_r;
                deltas[ode_id(nag,PREG,E1,i)]-=densities[ode_id(nag,PREG,E1,i)]*vacc_prov_p;

                deltas[ode_id(nag,VACC_LOW,E2,i)]+=densities[ode_id(nag,LOW,E2,i)]*vacc_prov;
                deltas[ode_id(nag,VACC_HIGH,E2,i)]+=densities[ode_id(nag,HIGH,E2,i)]*vacc_prov_r;
                deltas[ode_id(nag,VACC_PREG,E2,i)]+=densities[ode_id(nag,PREG,E2,i)]*vacc_prov_p;
                deltas[ode_id(nag,LOW,E2,i)]-=densities[ode_id(nag,LOW,E2,i)]*vacc_prov;
                deltas[ode_id(nag,HIGH,E2,i)]-=densities[ode_id(nag,HIGH,E2,i)]*vacc_prov_r;
                deltas[ode_id(nag,PREG,E2,i)]-=densities[ode_id(nag,PREG,E2,i)]*vacc_prov_p;

                deltas[ode_id(nag,VACC_LOW,I1,i)]+=densities[ode_id(nag,LOW,I1,i)]*vacc_prov;
                deltas[ode_id(nag,VACC_HIGH,I1,i)]+=densities[ode_id(nag,HIGH,I1,i)]*vacc_prov_r;
                deltas[ode_id(nag,VACC_PREG,I1,i)]+=densities[ode_id(nag,PREG,I1,i)]*vacc_prov_p;
                deltas[ode_id(nag,LOW,I1,i)]-=densities[ode_id(nag,LOW,I1,i)]*vacc_prov;
                deltas[ode_id(nag,HIGH,I1,i)]-=densities[ode_id(nag,HIGH,I1,i)]*vacc_prov_r;
                deltas[ode_id(nag,PREG,I1,i)]-=densities[ode_id(nag,PREG,I1,i)]*vacc_prov_p;

                deltas[ode_id(nag,VACC_LOW,I2,i)]+=densities[ode_id(nag,LOW,I2,i)]*vacc_prov;
                deltas[ode_id(nag,VACC_HIGH,I2,i)]+=densities[ode_id(nag,HIGH,I2,i)]*vacc_prov_r;
                deltas[ode_id(nag,VACC_PREG,I2,i)]+=densities[ode_id(nag,PREG,I2,i)]*vacc_prov_p;
                deltas[ode_id(nag,LOW,I2,i)]-=densities[ode_id(nag,LOW,I2,i)]*vacc_prov;
                deltas[ode_id(nag,HIGH,I2,i)]-=densities[ode_id(nag,HIGH,I2,i)]*vacc_prov_r;
                deltas[ode_id(nag,PREG,I2,i)]-=densities[ode_id(nag,PREG,I2,i)]*vacc_prov_p;

                deltas[ode_id(nag,VACC_LOW,R,i)]+=densities[ode_id(nag,LOW,R,i)]*vacc_prov+densities[ode_id(nag,LOW,S,i)]*vacc_prov*vaccine_efficacy[nag*LOW+i];
                deltas[ode_id(nag,VACC_HIGH,R,i)]+=densities[ode_id(nag,HIGH,R,i)]*vacc_prov_r+densities[ode_id(nag,HIGH,S,i)]*vacc_prov_r*vaccine_efficacy[nag*HIGH+i];
                deltas[ode_id(nag,VACC_PREG,R,i)]+=densities[ode_id(nag,PREG,R,i)]*vacc_prov_p+densities[ode_id(nag,PREG,S,i)]*vacc_prov_p*vaccine_efficacy[nag*HIGH+i];
                deltas[ode_id(nag,LOW,R,i)]-=densities[ode_id(nag,LOW,R,i)]*vacc_prov;
                deltas[ode_id(nag,HIGH,R,i)]-=densities[ode_id(nag,HIGH,R,i)]*vacc_prov_r;
                deltas[ode_id(nag,PREG,R,i)]-=densities[ode_id(nag,PREG,R,i)]*vacc_prov_p;
            }
        }
        return deltas;
    }

    inline Eigen::VectorXd new_cases( 
            Eigen::VectorXd &densities,
            const boost::posix_time::ptime &start_time,
            const boost::posix_time::ptime &end_time, 
            boost::posix_time::time_duration &dt,
            const Eigen::VectorXd &Npop,
            const Eigen::MatrixXd &vaccine_rates, // If empty, rate of zero is assumed
            const Eigen::VectorXd &vaccine_efficacy,
            const Eigen::MatrixXd &transmission_regular,
            double a1, double a2, double g1, double g2
            )
    {
        namespace bt = boost::posix_time;

        double h_step = dt.hours()/24.0;

        const size_t nag = transmission_regular.cols();
        Eigen::VectorXd results = Eigen::VectorXd::Zero(nag*3);

        Eigen::VectorXd deltas( nag*group_types.size()*
                seir_types.size() );

        auto t = 0.0;
        auto time_left = (end_time-start_time).hours()/24.0;

        auto ode_func = [&]( const Eigen::VectorXd &y, const double dummy )
        {
            return flu_ode( deltas, y, 
                    Npop, vaccine_rates, vaccine_efficacy,
                    transmission_regular, a1, a2, g1, g2 );
        };

        while (t < time_left)
        {
            auto prev_t = t;
            /*densities = ode::rkf45_astep( std::move(densities), ode_func,
                        h_step, t, time_left, 5 );*/
            densities = ode::step( std::move(densities), ode_func,
                        h_step, t, time_left );
            //Rcpp::Rcout << h_step << " " << t << " " << time_left << std::endl;

            results.block( 0, 0, nag, 1 ) += a2*(densities.segment(ode_id(nag,VACC_LOW,E2),nag)+densities.segment(ode_id(nag,LOW,E2),nag))*(t-prev_t);
            results.block( nag, 0, nag, 1 ) += a2*(densities.segment(ode_id(nag,VACC_HIGH,E2),nag)+densities.segment(ode_id(nag,HIGH,E2),nag))*(t-prev_t);
            results.block( 2*nag, 0, nag, 1 ) += a2*(densities.segment(ode_id(nag,VACC_PREG,E2),nag)+densities.segment(ode_id(nag,PREG,E2),nag))*(t-prev_t);
        }
        return results;
    }

    cases_t one_year_SEIR_with_vaccination(
            const Eigen::VectorXd &Npop,  
            const Eigen::VectorXd &seeding_infectious, 
            const double tlatent, const double tinfectious, 
            const Eigen::VectorXd &s_profile, 
            const Eigen::MatrixXd &contact_regular, double transmissibility,
            const vaccine::vaccine_t &vaccine_programme,
            size_t minimal_resolution, 
            const boost::posix_time::ptime &starting_time )
    {
        // This splits seeding_infectious into a risk groups
        // and then calls to the more general infectionODE function
        Eigen::MatrixXd risk_proportions = Eigen::MatrixXd( 
                2, seeding_infectious.size() );
        risk_proportions << 
            0.021, 0.055, 0.098, 0.087, 0.092, 0.183, 0.45, 
            0, 0, 0, 0, 0, 0, 0;

        auto seed_vec = flu::data::separate_into_risk_groups( 
                seeding_infectious, risk_proportions  );

        return infectionODE(
            Npop,  
            seed_vec, 
            tlatent, tinfectious, 
            s_profile, 
            contact_regular, transmissibility,
            vaccine_programme,
            minimal_resolution, 
            starting_time );
    }

    cases_t infectionODE(
            const Eigen::VectorXd &Npop,  
            const Eigen::VectorXd &seed_vec, 
            const double tlatent, const double tinfectious, 
            const Eigen::VectorXd &s_profile, 
            const Eigen::MatrixXd &contact_regular, 
            double transmissibility,
            const vaccine::vaccine_t &vaccine_programme,
            const std::vector<boost::posix_time::ptime> &times )
    {
        namespace bt = boost::posix_time;
 
        assert( s_profile.size() == contact_regular.rows() );

        const size_t nag = contact_regular.rows(); // No. of age groups

        Eigen::VectorXd densities = Eigen::VectorXd::Zero( 
                nag*group_types.size()*
                seir_types.size() );


        double a1, a2, g1, g2 /*, surv[7]={0,0,0,0,0,0,0}*/;
        a1=2/tlatent;
        a2=a1;
        g1=2/tinfectious;
        g2=g1;

        int date_id = -1;

        /*initialisation, transmission matrix*/
        Eigen::MatrixXd transmission_regular(contact_regular);
        for(int i=0;i<transmission_regular.rows();i++)
        {
            for(int j=0;j<transmission_regular.cols();j++) {
                transmission_regular(i,j)*=transmissibility*s_profile[i];
            }
        }

        /*initialisation, densities.segment(ode_id(nag,VACC_LOW,S),nag),E,I,densities.segment(ode_id(nag,VACC_LOW,R),nag)*/
        for(size_t i=0;i<nag;i++)
        {
            densities[ode_id(nag,LOW,E1,i)]=seed_vec[i];
            densities[ode_id(nag,HIGH,E1,i)]=seed_vec[i+nag];
            densities[ode_id(nag,PREG,E1,i)]=seed_vec[i+2*nag];

            densities[ode_id(nag,LOW,S,i)]=Npop[i]-densities[ode_id(nag,LOW,E1,i)];
            densities[ode_id(nag,HIGH,S,i)]=Npop[i+nag]-densities[ode_id(nag,HIGH,E1,i)];
            densities[ode_id(nag,PREG,S,i)]=Npop[i+2*nag]-densities[ode_id(nag,PREG,E1,i)];
        }

        auto current_time = times[0];

        cases_t cases;
        cases.cases = Eigen::MatrixXd::Zero( times.size()-1, 
                contact_regular.cols()*group_types.size()/2);
        cases.times = times;
        cases.times.erase( cases.times.begin() );

        size_t step_count = 0;
        static bt::time_duration dt = bt::hours( 6 );
        bool time_changed_for_vacc = false;
        auto next_time = current_time;
        auto start_time = current_time;
        while (step_count<cases.times.size())
        {
            next_time = cases.times[step_count];
            if (time_changed_for_vacc) 
            {
                // Previous iteration time was changed, now need to
                // go back to old situation
                time_changed_for_vacc = false;
            }

            
            //Rcpp::Rcout << "cTime: " << current_time << std::endl;
            //Rcpp::Rcout << "Time: " << next_time << std::endl;

            Eigen::VectorXd vacc_rates;
            if (vaccine_programme.dates.size() > 0)
            {
                while (date_id < ((int)vaccine_programme.dates.size())-1 && 
                        current_time == vaccine_programme.dates[date_id+1] )
                {
                    ++date_id;
                }
            } else {
                // Legacy mode
                date_id=floor((current_time-start_time).hours()/24.0-44);
            }

            if (date_id < ((int)vaccine_programme.dates.size())-1 && 
                        date_id >= -1 && 
                    next_time > vaccine_programme.dates[date_id+1] )
            {
                next_time = 
                    vaccine_programme.dates[date_id+1];
                time_changed_for_vacc = true;
            }

            if (date_id >= 0 &&
                    date_id < vaccine_programme.calendar.rows() )
                vacc_rates = vaccine_programme.calendar.row(date_id); 
            //Rcpp::Rcout << "Densities " << densities << std::endl;
            auto n_cases = new_cases( densities, current_time,
                    next_time, dt,
                    Npop,
                    vacc_rates,
                    vaccine_programme.efficacy,
                    transmission_regular,
                    a1, a2, g1, g2 );

            /* DEBUG This is a good sanity check if run into problems
            for( size_t i=0; i < densities.size(); ++i)
            {
                if (densities[i]<0)
                    ::Rf_error( "Some densities below zero" );
            }
            */

            current_time = next_time;
            //Rcpp::Rcout << "N cases" << n_cases << std::endl;

            assert(step_count < cases.cases.rows());

            cases.cases.row(step_count) += n_cases;
            if (!time_changed_for_vacc) 
            {
                ++step_count;
            }
        }
        return cases;
    } 

    cases_t infectionODE(
            const Eigen::VectorXd &Npop,  
            const Eigen::VectorXd &seed_vec, 
            const double tlatent, const double tinfectious, 
            const Eigen::VectorXd &s_profile, 
            const Eigen::MatrixXd &contact_regular, double transmissibility,
            const vaccine::vaccine_t &vaccine_programme,
            size_t minimal_resolution, 
            const boost::posix_time::ptime &starting_time )
    {
 
        namespace bt = boost::posix_time;
        auto current_time = starting_time;
        if (to_tm(current_time).tm_year==70 && 
                vaccine_programme.dates.size()!=0)
        {
            current_time = getTimeFromWeekYear( 35, 
                vaccine_programme.dates[0].date().year() );
        }

        auto start_time = current_time;
        auto end_time = current_time + bt::hours(364*24);

        auto next_time = current_time;

        std::vector<boost::posix_time::ptime> times;
        times.push_back(start_time);
        while(next_time < end_time)
        {
            next_time += bt::hours(minimal_resolution);
            times.push_back( next_time );
        }
        return infectionODE( Npop, seed_vec, tlatent, tinfectious,
                s_profile, contact_regular,
                transmissibility, vaccine_programme,
                times );
    }

    Eigen::MatrixXd days_to_weeks_11AG(const cases_t &simulation)
    {

        size_t weeks =  (simulation.times.back() - simulation.times.front())
            .hours()/(24*7) + 1;
        auto result_days = simulation.cases;
        /*initialisation*/
        Eigen::MatrixXd result_weeks = 
            Eigen::MatrixXd::Zero( weeks, 11 );

        size_t j = 0;
        for(size_t i=0; i<weeks; i++)
        {
            auto startWeek = simulation.times[j];
            while( j < simulation.times.size() &&
                    (simulation.times[j]-startWeek).hours()/(24.0)<7.0 )
            {
                result_weeks(i,0)+=result_days(j,0)+result_days(j,10);
                result_weeks(i,1)+=result_days(j,1)+result_days(j,11);
                result_weeks(i,2)+=result_days(j,2)+result_days(j,12);
                result_weeks(i,3)+=result_days(j,3)+result_days(j,13);
                result_weeks(i,4)+=result_days(j,4)+result_days(j,14);
                result_weeks(i,5)+=result_days(j,5)+result_days(j,15);
                result_weeks(i,6)+=result_days(j,6)+result_days(j,16);
                result_weeks(i,7)+=result_days(j,7)+result_days(j,17);
                result_weeks(i,8)+=result_days(j,8)+result_days(j,18);
                result_weeks(i,9)+=result_days(j,9)+result_days(j,19);
                result_weeks(i,10)+=result_days(j,10)+result_days(j,20);
                  ++j;
            }
        }

        return result_weeks;
    }

    Eigen::MatrixXd days_to_weeks_11AG(const cases_t &simulation,
        const Eigen::MatrixXd &mapping, size_t no_data)
    {

        size_t weeks =  (simulation.times.back() - simulation.times.front())
            .hours()/(24*7) + 1;
        auto result_days = simulation.cases;
        /*initialisation*/
        Eigen::MatrixXd result_weeks = 
            Eigen::MatrixXd::Zero(weeks, no_data);

        size_t j = 0;
        for(size_t i=0; i<weeks; i++)
        {
            auto startWeek = simulation.times[j];
            while( j < simulation.times.size() &&
                    (simulation.times[j]-startWeek).hours()/(24.0)<7.0 )
            {
              for(size_t k = 0; k < mapping.rows(); ++k)
                result_weeks(i,(size_t) mapping(k,1)) += mapping(k,2)*result_days(j,(size_t) mapping(k,0));
              ++j;
            }
        }

        return result_weeks;
    }

    long double binomial_log_likelihood( double epsilon, 
            size_t predicted, double population_size, 
            int ili_cases, int ili_monitored,
            int confirmed_positive, int confirmed_samples
             )
    {
        auto pf = (1.0/epsilon)*((double) predicted)/population_size;
        long double prob = 0;
        for (size_t mplus = 0; mplus <= (size_t)ili_cases; ++mplus)
        {
            auto pn = ((double)mplus)/ili_cases;
            prob += R::dbinom( mplus, ili_cases, pf, 0 )*
                R::dbinom( confirmed_positive, confirmed_samples,
                        pn, 0 );
        }
        if (prob == 0 || !std::isfinite(prob))
        {
            /*Rcpp::cout << epsilon << ", " << predicted << ", " <<
                population_size << ", " << ili_cases << ", " << 
                confirmed_positive << ", " confirmed_samples <<
                std::endl;*/

            return log(1e-100);
        }
        return log(prob);
    }

    double binomial_log_likelihood_year(const std::vector<double> &eps, 
            const Eigen::MatrixXd &result_by_week,
            const Eigen::MatrixXi &ili, const Eigen::MatrixXi &mon_pop, 
            const Eigen::MatrixXi &n_pos, const Eigen::MatrixXi &n_samples, 
            double * pop_11AG_RCGP)
    {
        long double result=0.0;
        for(int i=0;i<11;i++)
        {
            auto epsilon=eps[i];
            for(int week=0;week<result_by_week.rows();week++)
            {
                result += binomial_log_likelihood( epsilon, 
                        result_by_week(week,i), pop_11AG_RCGP[i],
                        ili(week,i), mon_pop(week,i),
                        n_pos(week,i), n_samples(week,i) );
            }
            
        }
        return result;
    }


    long double log_likelihood( double epsilon, double psi, 
            size_t predicted, double population_size, 
            int ili_cases, int ili_monitored,
            int confirmed_positive, int confirmed_samples, 
            int depth )
    {
        int Z_in_mon=(int)round(predicted*ili_monitored/population_size);
        int n=confirmed_samples;
        int m=ili_cases;

        /*h.init=max(n.plus-Z.in.mon,0)*/
        int h_init;
        if(confirmed_positive>Z_in_mon)
            h_init=confirmed_positive-Z_in_mon;
        else
            h_init=0;

        if(h_init>depth)
        {
            return -(h_init-depth)*
                std::numeric_limits<double>::max()/1e6;
        }

        /*define the first aij*/
        long double laij=log(epsilon)*confirmed_positive-psi*ili_monitored*epsilon;

        if(confirmed_positive<n)
            for(int g=confirmed_positive; g<n; g++) {
                laij += log(m-g);
            }

        if((Z_in_mon==confirmed_positive)&&(Z_in_mon>0))
            for(int g=1; g<=confirmed_positive;g++) {
                laij += log(g);
            }

        if((confirmed_positive>0)&&(confirmed_positive<Z_in_mon))
            for(int g=0;g<confirmed_positive;g++) {
                laij+=log(Z_in_mon-g);
            }

        if((Z_in_mon>0)&&(confirmed_positive>Z_in_mon))
            for(int g=0; g<Z_in_mon;g++) {
                laij+=log(confirmed_positive-g);
            }

        if(confirmed_positive>Z_in_mon) {
            laij+=log(psi*ili_monitored)*(confirmed_positive-Z_in_mon);
        }

        if(confirmed_positive<Z_in_mon)
            laij+=log(1-epsilon)*(Z_in_mon-confirmed_positive);

        /*store the values of the first aij on the current line*/
        long double laij_seed=laij;
        int k_seed=confirmed_positive;

        auto llikelihood_AG_week=laij;

        /*Calculation of the first line*/
        int max_m_plus;
        if(Z_in_mon+h_init>m-n+confirmed_positive)
            max_m_plus=m-n+confirmed_positive;
        else
            max_m_plus=Z_in_mon+h_init;

        if(max_m_plus>confirmed_positive)
            for(int k=confirmed_positive+1;k<=max_m_plus;k++)
            {
                laij+=log(k) + log(epsilon) + log(m-k-n+confirmed_positive+1)+log(Z_in_mon-k+h_init+1)-log(m-k+1)- 
                  log(k-confirmed_positive) - log(k-h_init) - log(1-epsilon);
                llikelihood_AG_week = safe_sum_log(laij, llikelihood_AG_week);
            }

        /*top_sum=min(depth,m-n+confirmed_positive)*/
        int top_sum;
        if(depth<m-n+confirmed_positive)
            top_sum=depth;
        else
            top_sum=m-n+confirmed_positive;

        if(h_init<top_sum)
            for(int h=h_init+1;h<=top_sum;h++)
            {
                if(h>confirmed_positive) /*diagonal increment*/
                {
                    k_seed++;
                    laij_seed+=log(k_seed)+log(m-k_seed-n+confirmed_positive+1) + log(psi) +
                      log(epsilon) + log(ili_monitored)- log(m-k_seed+1) - 
                      log(k_seed-confirmed_positive) - log(h);
                }

                if(h<=confirmed_positive) /*vertical increment*/ {
                    laij_seed+=log(k_seed-h+1) + log(psi) + log(ili_monitored) + log(1-epsilon) -
                      log(Z_in_mon-k_seed+h) - log(h);
                }

                laij=laij_seed;
                llikelihood_AG_week = safe_sum_log(laij, llikelihood_AG_week);

                /*calculation of the line*/
                if(Z_in_mon+h>m-n+confirmed_positive)
                    max_m_plus=m-n+confirmed_positive;
                else
                    max_m_plus=Z_in_mon+h;

                if(max_m_plus>k_seed)
                    for(int k=k_seed+1;k<=max_m_plus;k++)
                    {
                        laij+=log(k) + log(m-k-n+confirmed_positive+1) + log(Z_in_mon-k+h+1) +
                          log(epsilon) - log(m-k+1) - log(k-confirmed_positive) -log(k-h) -
                          log(1-epsilon);
                        llikelihood_AG_week = safe_sum_log(laij, llikelihood_AG_week);
                    }
            }

        //auto ll = log(likelihood_AG_week);
        auto ll = llikelihood_AG_week;
        if (!std::isfinite(ll))
        {
            /*Rcpp::Rcerr << "Numerical error detected for week " 
              << week << " and age group " << i << std::endl;
              Rcpp::Rcerr << "Predicted number of cases is: "
              << result_by_week(week,i) << std::endl;*/
            ll = -std::numeric_limits<double>::max()/1e7;
        }
        return ll;
    }

    double log_likelihood_hyper_poisson(const Eigen::VectorXd &eps, 
            double psi, const Eigen::MatrixXd &result_by_week,
            const Eigen::MatrixXi &ili, const Eigen::MatrixXi &mon_pop, 
            const Eigen::MatrixXi &n_pos, const Eigen::MatrixXi &n_samples, 
            //int * n_ILI, int * mon_popu, int * n_posi, int * n_sampled, 
            const Eigen::VectorXd &pop_11AG_RCGP, int depth)
    {
        long double result=0.0;
        for(int i=0;i<pop_11AG_RCGP.size();i++)
        {
            auto epsilon=eps(i);
            for(int week=0;week<result_by_week.rows();week++)
            {
                result += log_likelihood( epsilon, psi, 
                        result_by_week(week,i), pop_11AG_RCGP(i),
                        ili(week,i), mon_pop(week,i),
                        n_pos(week,i), n_samples(week,i), depth );
            }
            
        }
        return(result);
    }

    /// Return the log prior probability of the proposed parameters - current parameters
    //
    // \param susceptibility whether to use the prior based on 2003/04
    double log_prior( const parameter_set &proposed,
            const parameter_set &current,
            bool susceptibility ) {

        // Parameters should be valid
        if( 
                proposed.epsilon[0] <= 0 || proposed.epsilon[0] >= 1 ||
                proposed.epsilon[2] <= 0 || proposed.epsilon[2] >= 1 ||
                proposed.epsilon[4] <= 0 || proposed.epsilon[4] >= 1 ||
                proposed.psi < 0 || proposed.psi > 1 ||
                proposed.transmissibility < 0 ||
                proposed.susceptibility[0] < 0 || proposed.susceptibility[1] > 1 ||
                proposed.susceptibility[3] < 0 || proposed.susceptibility[3] > 1 ||
                proposed.susceptibility[6] < 0 || proposed.susceptibility[6] > 1 ||
                proposed.init_pop<log(0.00001) || proposed.init_pop>log(10)
          )
            return log(0);

        double log_prior = 0;
        if (!susceptibility)
        {
            /*Prior for the transmissibility; year other than 2003/04*/
            /*correction for a normal prior with mu=0.1653183 and sd=0.02773053*/
            /*prior on q*/
            log_prior=(current.transmissibility-proposed.transmissibility)*(current.transmissibility+proposed.transmissibility-0.3306366)*650.2099;
        } else {

            /*prior on the susceptibility (year 2003/04)*/

            /*correction for a normal prior with mu=0.688 and sd=0.083 for the 0-14 */
            log_prior=(current.susceptibility[0]-proposed.susceptibility[0])*(current.susceptibility[0]+proposed.susceptibility[0]-1.376)*145.1589/2;
            /*correction for a normal prior with mu=0.529 and sd=0.122 for the 15-64 */
            log_prior+=(current.susceptibility[3]-proposed.susceptibility[3])*(current.susceptibility[3]+proposed.susceptibility[3]-1.058)*67.18624/2;
            /*correction for a normal prior with mu=0.523 and sd=0.175 for the 65+ */
            log_prior+=(current.susceptibility[6]-proposed.susceptibility[6])*(current.susceptibility[6]+proposed.susceptibility[6]-1.046)*32.65306/2;
        }

        /*Prior for the ascertainment probabilities*/

        /*correct for the prior from serology season (lognormal):"0-14" lm=-4.493789, ls=0.2860455*/
        log_prior += log(current.epsilon[0])-log(proposed.epsilon[0])+(log(current.epsilon[0])-log(proposed.epsilon[0]))*(log(current.epsilon[0])+log(proposed.epsilon[0])+8.987578)*6.110824;

        /*correct for the prior from serology season (lognormal):"15-64" lm=-4.117028, ls=0.4751615*/
        log_prior += log(current.epsilon[2])-log(proposed.epsilon[2])+(log(current.epsilon[2])-log(proposed.epsilon[2]))*(log(current.epsilon[2])+log(proposed.epsilon[2])+8.234056)*2.21456;

        /*correct for the prior from serology season (lognormal):"65+" lm=-2.977965, ls=1.331832*/
        log_prior += log(current.epsilon[4])-log(proposed.epsilon[4])+(log(current.epsilon[4])-log(proposed.epsilon[4]))*(log(current.epsilon[4])+log(proposed.epsilon[4])+5.95593)*0.2818844;
        return log_prior;
    }

    double log_prior( const Eigen::VectorXd &proposed,
            const Eigen::VectorXd &current,
            bool susceptibility ) {

        // Parameters should be valid
        if( 
                proposed[0] <= 0 || proposed[0] >= 1 ||
                proposed[1] <= 0 || proposed[1] >= 1 ||
                proposed[2] <= 0 || proposed[2] >= 1 ||
                proposed[3] < 0 || proposed[3] > 1 ||
                proposed[4] < 0 ||
                proposed[5] < 0 || proposed[5] > 1 ||
                proposed[6] < 0 || proposed[6] > 1 ||
                proposed[7] < 0 || proposed[7] > 1 ||
                proposed[8]<log(0.00001) || proposed[8]>log(10)
          )
            return log(0);

        double log_prior = 0;
        if (!susceptibility)
        {
            /*Prior for the transmissibility; year other than 2003/04*/
            /*correction for a normal prior with mu=0.1653183 and sd=0.02773053*/
            /*prior on q*/
            log_prior=(current[4]-proposed[4])*(current[4]+proposed[4]-0.3306366)*650.2099;
        } else {

            /*prior on the susceptibility (year 2003/04)*/

            /*correction for a normal prior with mu=0.688 and sd=0.083 for the 0-14 */
            log_prior=(current[5]-proposed[5])*(current[5]+proposed[5]-1.376)*145.1589/2;
            /*correction for a normal prior with mu=0.529 and sd=0.122 for the 15-64 */
            log_prior+=(current[6]-proposed[6])*(current[6]+proposed[6]-1.058)*67.18624/2;
            /*correction for a normal prior with mu=0.523 and sd=0.175 for the 65+ */
            log_prior+=(current[7]-proposed[7])*(current[7]+proposed[7]-1.046)*32.65306/2;
        }

        /*Prior for the ascertainment probabilities*/

        /*correct for the prior from serology season (lognormal):"0-14" lm=-4.493789, ls=0.2860455*/
        log_prior += log(current[0])-log(proposed[0])+(log(current[0])-log(proposed[0]))*(log(current[0])+log(proposed[0])+8.987578)*6.110824;

        /*correct for the prior from serology season (lognormal):"15-64" lm=-4.117028, ls=0.4751615*/
        log_prior += log(current[1])-log(proposed[1])+(log(current[1])-log(proposed[1]))*(log(current[1])+log(proposed[1])+8.234056)*2.21456;

        /*correct for the prior from serology season (lognormal):"65+" lm=-2.977965, ls=1.331832*/
        log_prior += log(current[2])-log(proposed[2])+(log(current[2])-log(proposed[2]))*(log(current[2])+log(proposed[2])+5.95593)*0.2818844;
        return log_prior;
    }
}
