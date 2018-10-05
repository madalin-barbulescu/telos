/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include "eosio.system.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/print.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/transaction.hpp>
#include <eosio.token/eosio.token.hpp>

#include <algorithm>
#include <cmath>

#define VOTE_VARIATION 0.1
#define TWELVE_HOURS_US 43200000000
#define SIX_MINUTES_US 420000000 // debug version
#define SIX_HOURS_US 21600000000
#define MAX_PRODUCERS 51
#define TOP_PRODUCERS 21

namespace eosiosystem {
   using namespace eosio;
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::bytes;
   using eosio::print;
   using eosio::singleton;
   using eosio::transaction;
   /**
    *  This method will create a producer_config and producer_info object for 'producer'
    *
    *  @pre producer is not already registered
    *  @pre producer to register is an account
    *  @pre authority of producer to register
    *
    */
   void system_contract::regproducer( const account_name producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      eosio_assert( url.size() < 512, "url too long" );
      eosio_assert( producer_key != eosio::public_key(), "public key should not be the default value" );
      require_auth( producer );

      auto prod = _producers.find( producer );

      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
               info.producer_key = producer_key;
               info.is_active    = true;
               info.url          = url;
               info.location     = location;
            });
      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
               info.owner         = producer;
               info.total_votes   = 0;
               info.producer_key  = producer_key;
               info.is_active     = true;
               info.url           = url;
               info.location      = location;
         });
      }
   }

   void system_contract::unregprod( const account_name producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer, "producer not found" );

      _producers.modify( prod, 0, [&]( producer_info& info ){
            info.deactivate();
      });
   }

   void system_contract::setBPsRotation(account_name bpOut, account_name sbpIn) {
      _grotations.bp_currently_out = bpOut;
      _grotations.sbp_currently_in = sbpIn;
   }

   void system_contract::updateRotationTime(block_timestamp block_time){
      _grotations.last_rotation_time = block_time;
      _grotations.next_rotation_time = block_timestamp(block_time.to_time_point() + time_point(microseconds(SIX_MINUTES_US)));
   } 

   void system_contract::update_elected_producers( block_timestamp block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<N(prototalvote)>();

      auto totalActiveVotedProds = std::distance(idx.begin(), idx.end());
      totalActiveVotedProds = totalActiveVotedProds > MAX_PRODUCERS ? MAX_PRODUCERS : totalActiveVotedProds;
      
      std::vector<eosio::producer_key> prods;
      prods.reserve(totalActiveVotedProds);

      //add active producers with vote > 0
      for ( auto it = idx.cbegin(); it != idx.cend() && prods.size() < totalActiveVotedProds && it->total_votes > 0 && it->active(); ++it ) {
         prods.emplace_back( eosio::producer_key{it->owner, it->producer_key} );
      }

      totalActiveVotedProds = prods.size();
      
      vector<eosio::producer_key>::iterator it_bp = prods.end();
      vector<eosio::producer_key>::iterator it_sbp = prods.end();

      if (_grotations.next_rotation_time <= block_time) {
        // restart all missed blocks to bps and sbps
        for (int i = 0; i < prods.size(); i++) {
          auto pitr = _producers.find(prods[i].producer_name);
          if (pitr != _producers.end() && pitr->active()) {
            _producers.modify(pitr, 0, [&](auto &p) { 
              p.missed_blocks = 0;
            });  
          }
        }

        if (totalActiveVotedProds > TOP_PRODUCERS) {
          _grotations.bp_out_index = _grotations.bp_out_index >= TOP_PRODUCERS - 1 ? 0 : _grotations.bp_out_index + 1;
          _grotations.sbp_in_index = _grotations.sbp_in_index >= totalActiveVotedProds - 1 ? TOP_PRODUCERS : _grotations.sbp_in_index + 1;

          account_name bp_name = prods[_grotations.bp_out_index].producer_name;
          account_name sbp_name = prods[_grotations.sbp_in_index].producer_name;

          it_bp = std::find_if(prods.begin(), prods.end(), [&bp_name](const eosio::producer_key &g) {
            return g.producer_name == bp_name; 
          });   

          it_sbp = std::find_if(prods.begin(), prods.end(), [&sbp_name](const eosio::producer_key &g) {
            return g.producer_name == sbp_name; 
          });


          print("\n sb_name: ", name{sbp_name});
          print("\n it_sbp: ", name{it_sbp->producer_name});

          if(it_bp == prods.end() && it_sbp == prods.end()) {
            setBPsRotation(0, 0);

            _grotations.bp_out_index = TOP_PRODUCERS;
            _grotations.sbp_in_index = MAX_PRODUCERS + 1;

            it_bp = prods.end();
            it_sbp = prods.end();

          } else {
            if(it_bp != prods.end() && it_sbp == prods.end()) {
              if(_grotations.sbp_in_index > totalActiveVotedProds - 1) {
                _grotations.sbp_in_index = TOP_PRODUCERS;
                 
                sbp_name = prods[_grotations.sbp_in_index].producer_name;
                it_sbp = std::find_if(prods.begin(), prods.end(), [&sbp_name](const eosio::producer_key &g) {
                  return g.producer_name == sbp_name; 
                });
              }
            } else if (it_bp == prods.end() && it_sbp != prods.end()) {
              if(_grotations.bp_out_index > TOP_PRODUCERS - 1) {
                _grotations.bp_out_index = 0;
                
                bp_name = prods[_grotations.bp_out_index].producer_name;
                it_bp = std::find_if(prods.begin(), prods.end(), [&bp_name](const eosio::producer_key &g) {
                  return g.producer_name == bp_name; 
                });
              }
            } else {
              setBPsRotation(bp_name, sbp_name);
            }
          }
      } 
      updateRotationTime(block_time);
      }
      else {
        if(_grotations.bp_currently_out != 0 && _grotations.sbp_currently_in != 0) {
          auto bp_name = _grotations.bp_currently_out;
          it_bp = std::find_if(prods.begin(), prods.end(), [&bp_name](const eosio::producer_key &g) {
            return g.producer_name == bp_name; 
          });

          auto sbp_name = _grotations.sbp_currently_in;
          it_sbp = std::find_if(prods.begin(), prods.end(), [&sbp_name](const eosio::producer_key &g) {
            return g.producer_name == sbp_name; 
          });

          auto _bp_index = std::distance(prods.begin(), it_bp);
          auto _sbp_index = std::distance(prods.begin(), it_sbp);

          if(it_bp == prods.end() || it_sbp == prods.end()) {
              setBPsRotation(0, 0);

            if(totalActiveVotedProds < TOP_PRODUCERS) {
              _grotations.bp_out_index = TOP_PRODUCERS;
              _grotations.sbp_in_index = MAX_PRODUCERS+1;
            }
          } else if (totalActiveVotedProds > TOP_PRODUCERS && (!is_in_range(_bp_index, 0, TOP_PRODUCERS) || !is_in_range(_sbp_index, TOP_PRODUCERS, MAX_PRODUCERS))) {
              setBPsRotation(0, 0);
          }
        }
    }

      print("\nsbp name: ", name{it_sbp->producer_name});
      print("\nsbp index: ", _grotations.sbp_in_index);
      print("\nsbp name on prods array: ", name{prods[_grotations.sbp_in_index].producer_name});  

      std::vector<eosio::producer_key>  top_producers;
      
      //Rotation
      if(it_bp != prods.end() && it_sbp != prods.end()) {
        for ( auto pIt = prods.begin(); pIt != prods.end(); ++pIt) {
          auto i = std::distance(prods.begin(), pIt); 
          print("\ni-> ", i);
          if(i > TOP_PRODUCERS - 1) break;

          if(pIt->producer_name == it_bp->producer_name) {
            print("\nprod sbp added to schedule -> ", name{it_sbp->producer_name});
            if(it_sbp->producer_name == prods[_grotations.sbp_in_index].producer_name) top_producers.emplace_back(*it_sbp);
            else  top_producers.emplace_back(prods[_grotations.sbp_in_index]);
          } else {
            print("\nprod bp added to schedule -> ", name{pIt->producer_name});
            top_producers.emplace_back(*pIt);
          } 
        }
      } 
      else {
        top_producers = prods;
        if(prods.size() > TOP_PRODUCERS) top_producers.resize(TOP_PRODUCERS);
        else top_producers.resize(prods.size());
      }

      // if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
      //    return;
      // }

      // sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );
      bytes packed_schedule = pack(top_producers);

      if( set_proposed_producers( packed_schedule.data(),  packed_schedule.size() ) >= 0 ) {
        print("\nschedule was proposed");
        
        for( const auto& item : top_producers ){
         print("\n*producer: ", name{item.producer_name});
        }
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );
      }
   }
   
   /*
   * This function caculates the inverse weight voting. 
   * The maximum weighted vote will be reached if an account votes for the maximum number of registered producers (up to 30 in total).  
   */   
   double system_contract::inverseVoteWeight(double staked, double amountVotedProducers) {
     if (amountVotedProducers == 0.0) {
       return 0;
     }

     auto totalProducers = 0;
     for (const auto &prod : _producers) {
       if(prod.active()) { 
         totalProducers++;
       }

       // 30 max producers allowed to vote
       if(totalProducers >= 30){
         break;
       }
     }

     if(totalProducers == 0){
        return 0;
     }

     double k = 1 - VOTE_VARIATION;
     
     return (k * sin(M_PI_2 * (amountVotedProducers / totalProducers)) + VOTE_VARIATION) * double(staked);
   }


   bool system_contract::is_in_range(int32_t index, int32_t low_bound, int32_t up_bound) {
     return index >= low_bound && index < up_bound;
   } 

   /**
    *  @pre producers must be sorted from lowest to highest and must be registered and active
    *  @pre if proxy is set then no producers can be voted for
    *  @pre if proxy is set then proxy account must exist and be registered as a proxy
    *  @pre every listed producer or proxy must have been previously registered
    *  @pre voter must authorize this action
    *  @pre voter must have previously staked some EOS for voting
    *  @pre voter->staked must be up to date
    *
    *  @post every producer previously voted for will have vote reduced by previous vote weight
    *  @post every producer newly voted for will have vote increased by new vote amount
    *  @post prior proxy will proxied_vote_weight decremented by previous vote weight
    *  @post new proxy will proxied_vote_weight incremented by new vote weight
    *
    *  If voting for a proxy, the producer votes will not change until the proxy updates their own vote.
    */
   void system_contract::voteproducer(const account_name voter_name, const account_name proxy, const std::vector<account_name> &producers) {
     require_auth(voter_name);
     update_votes(voter_name, proxy, producers, true);
   }
   
   void system_contract::checkNetworkActivation(){
     if( _gstate.total_activated_stake >= min_activated_stake && _gstate.thresh_activated_stake_time == 0 ) {
            _gstate.thresh_activated_stake_time = current_time();
    }
   } 

   void system_contract::update_votes( const account_name voter_name, const account_name proxy, const std::vector<account_name>& producers, bool voting ) {
      //validate input
      if ( proxy ) {
         eosio_assert( producers.size() == 0, "cannot vote for producers and proxy at same time" );
         eosio_assert( voter_name != proxy, "cannot proxy to self" );
         require_recipient( proxy );
      } else {
         eosio_assert( producers.size() <= 30, "attempt to vote for too many producers" );
         for( size_t i = 1; i < producers.size(); ++i ) {
            eosio_assert( producers[i-1] < producers[i], "producer votes must be unique and sorted" );
         }
      }

      auto voter = _voters.find(voter_name);
      eosio_assert( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object
      eosio_assert( !proxy || !voter->is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      auto totalStaked = voter->staked;
      if(voter->is_proxy){
         totalStaked += voter->proxied_vote_weight;
      }

      // when unvoting, set the stake used for calculations to 0
      // since it is the equivalent to retracting your stake
      if(voting && !proxy && producers.size() == 0){
         totalStaked = 0;
      }

      // when a voter or a proxy votes or changes stake, the total_activated stake should be re-calculated
      // any proxy stake handling should be done when the proxy votes or on weight propagation
      // if(_gstate.thresh_activated_stake_time == 0 && !proxy && !voter->proxy){
      if(!proxy && !voter->proxy){
         _gstate.total_activated_stake += totalStaked - voter->last_stake;
         checkNetworkActivation();
      }

      auto new_vote_weight = inverseVoteWeight((double )totalStaked, (double) producers.size());
      boost::container::flat_map<account_name, pair<double, bool /*new*/> > producer_deltas;

      // print("\n Voter : ", voter->last_stake, " = ", voter->last_vote_weight, " = ", proxy, " = ", producers.size(), " = ", totalStaked, " = ", new_vote_weight);
      
      //Voter from second vote
      if ( voter->last_stake > 0 ) {

         //if voter account has set proxy to another voter account
         if( voter->proxy ) { 
            auto old_proxy = _voters.find( voter->proxy );

            eosio_assert( old_proxy != _voters.end(), "old proxy not found" ); //data corruption
               
            _voters.modify( old_proxy, 0, [&]( auto& vp ) {
               vp.proxied_vote_weight -= voter->last_stake;
            });

            // propagate weight here only when switching proxies
            // otherwise propagate happens in the case below
            if( proxy != voter->proxy ){ 
               // if(_gstate.thresh_activated_stake_time == 0){
                  _gstate.total_activated_stake += totalStaked - voter->last_stake;
                  checkNetworkActivation();
               // }

               propagate_weight_change( *old_proxy );
            }
         } else {
            for( const auto& p : voter->producers ) {
               auto& d = producer_deltas[p];
               d.first -= voter->last_vote_weight;
               d.second = false;
            }
         }
      }

      if( proxy ) {
         auto new_proxy = _voters.find( proxy );
         eosio_assert( new_proxy != _voters.end(), "invalid proxy specified" ); //if ( !voting ) { data corruption } else { wrong vote }
         eosio_assert( !voting || new_proxy->is_proxy, "proxy not found" );
        
         _voters.modify( new_proxy, 0, [&]( auto& vp ) {
            vp.proxied_vote_weight += voter->staked;
         });
         
         if((*new_proxy).last_vote_weight > 0){
            // if(_gstate.thresh_activated_stake_time == 0){
               _gstate.total_activated_stake += totalStaked - voter->last_stake;
               checkNetworkActivation();
            // }
            
            propagate_weight_change( *new_proxy );
         }
      } else {
         if( new_vote_weight >= 0 ) {
            for( const auto& p : producers ) {
               auto& d = producer_deltas[p]; 
               d.first += new_vote_weight;
               d.second = true;
            }
         }
      }

      for( const auto& pd : producer_deltas ) {
         auto pitr = _producers.find( pd.first );
         if( pitr != _producers.end() ) {
            eosio_assert( !voting || pitr->active() || !pd.second.second /* not from new set */, "producer is not currently registered" );
            _producers.modify( pitr, 0, [&]( auto& p ) {
               p.total_votes += pd.second.first;
               if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += pd.second.first;
            });
         } else {
            eosio_assert( !pd.second.second /* not from new set */, "producer is not registered" ); //data corruption
         }
      }

      _voters.modify( voter, 0, [&]( auto& av ) {
         av.last_vote_weight = new_vote_weight;
         av.last_stake = totalStaked;
         av.producers = producers;
         av.proxy     = proxy;
      });
   }

   /**
    *  An account marked as a proxy can vote with the weight of other accounts which
    *  have selected it as a proxy. Other accounts must refresh their voteproducer to
    *  update the proxy's weight.
    *
    *  @param isproxy - true if proxy wishes to vote on behalf of others, false otherwise
    *  @pre proxy must have something staked (existing row in voters table)
    *  @pre new state must be different than current state
    */
   void system_contract::regproxy( const account_name proxy, bool isproxy ) {
      require_auth( proxy );
      auto pitr = _voters.find(proxy);
      if ( pitr != _voters.end() ) {
         eosio_assert( isproxy != pitr->is_proxy, "action has no effect" );
         eosio_assert( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, 0, [&]( auto& p ) {
          p.is_proxy = isproxy;
        });    
         update_votes(pitr->owner, pitr->proxy, pitr->producers, true);
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
            p.owner  = proxy;
            p.is_proxy = isproxy;
       });
      }
   }


   void system_contract::propagate_weight_change(const voter_info &voter) {
      eosio_assert( voter.proxy == 0 || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy");
      
      auto totalStake = double(voter.staked);
      if(voter.is_proxy){
         totalStake += voter.proxied_vote_weight;
      } 
      double new_weight = inverseVoteWeight(totalStake, voter.producers.size());
    
      if (new_weight - voter.last_vote_weight > 1){
         if (voter.proxy) {
            if(voter.last_stake != totalStake){
               // this part should never happen since the function is called only on proxies
               auto &proxy = _voters.get(voter.proxy, "proxy not found"); // data corruption
               _voters.modify(proxy, 0, [&](auto &p) { 
                  p.proxied_vote_weight += totalStake - voter.last_stake;
               });
               
               propagate_weight_change(proxy);
            }
         } else {
            for (auto acnt : voter.producers) {
               auto &pitr = _producers.get(acnt, "producer not found"); // data corruption
               _producers.modify(pitr, 0, [&](auto &p) {
                  p.total_votes += new_weight;
                  _gstate.total_producer_vote_weight += new_weight;
               });
            }
         }
      }
      
      _voters.modify(voter, 0, [&](auto &v) { 
         v.last_vote_weight = new_weight; 
         v.last_stake = totalStake;
      });
   }

} /// namespace eosiosystem