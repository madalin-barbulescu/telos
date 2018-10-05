#pragma once

#include "eosio.system.hpp"
#include <eosiolib/print.hpp>
#include <eosiolib/producer_schedule.hpp>
#include <eosio.token/eosio.token.hpp>
#include <eosiolib/chain.h>
#include <algorithm>
#include <cmath>  

#define RESET_BLOCKS_PRODUCED 0
#define MAX_BLOCK_PER_CYCLE 12

namespace eosiosystem
{
using namespace eosio;

/**
* TELOS CHANGES:
*
* 1. Updated continuous_rate (inflation rate) to 2.5% --- DONE
* 2. Added producer_rate constant for BP/Standby payout --- DONE
* 3. Updated min_activated_stake to reflect TELOS 15% activation threshold --- DONE
* 4. Added worker_proposal_rate constant for Worker Proposal Fund --- DONE
* 5. Added min_unpaid_blocks_threshold constant for producer payout qualification --- DONE
*
* NOTE: A full breakdown of the calculated token supply and 15% activation threshold
* can be found at: https://docs.google.com/document/d/1K8w_Kd8Vmk_L0tAK56ETfAWlqgLo7r7Ae3JX25A5zVk/edit?usp=sharing
*/
const int64_t min_activated_stake = 28'570'987'3500; // calculated from max TLOS supply of 190,473,249 (fluctuating value until mainnet activation)
const double continuous_rate = 0.025;                // 2.5% annual inflation rate
const double producer_rate = 0.01;                   // 1% TLOS rate to BP/Standby
const double worker_rate = 0.015;                    // 1.5% TLOS rate to worker fund
const uint64_t min_unpaid_blocks_threshold = 342;    // Minimum unpaid blocks required to qualify for Producer level payout

// Calculated constants
const uint32_t blocks_per_year = 52 * 7 * 24 * 2 * 3600; // half seconds per year
const uint32_t seconds_per_year = 52 * 7 * 24 * 3600;
const uint32_t blocks_per_day = 2 * 24 * 3600;
const uint32_t blocks_per_hour = 2 * 3600;
const uint64_t useconds_per_day = 24 * 3600 * uint64_t(1000000);
const uint64_t useconds_per_year = seconds_per_year * 1000000ll;

bool system_contract::crossed_missed_blocks_threshold(uint32_t amountBlocksMissed) {
    //6hrs
    auto timeframe = (_grotations.next_rotation_time.to_time_point() - _grotations.last_rotation_time.to_time_point()).to_seconds();
   
    //get_active_producers returns the number of bytes populated
    account_name prods[21];
    uint32_t totalProds = get_active_producers(prods, sizeof(account_name) * 21) / 8;
    //Total blocks that can be produced in a cycle
    auto maxBlocksPerCycle = (totalProds - 1) * MAX_BLOCK_PER_CYCLE;
    //total block that can be produced in the current timeframe
    auto totalBlocksPerTimeframe = (maxBlocksPerCycle * timeframe) / (maxBlocksPerCycle / 2);
    //max blocks that can be produced by a single producer in a timeframe
    auto maxBlocksPerProducer = (totalBlocksPerTimeframe * MAX_BLOCK_PER_CYCLE) / maxBlocksPerCycle;
    //15% is the max allowed missed blocks per single producer
    auto thresholdMissedBlocks = maxBlocksPerProducer * 0.15;

    return amountBlocksMissed > thresholdMissedBlocks;
}

void system_contract::set_producer_block_produced(account_name producer, uint32_t amount) {
  auto pitr = _producers.find(producer);
  if (pitr != _producers.end()) {
      if(amount == 0) _producers.modify(pitr, 0, [&](auto &p) { p.blocks_per_cycle = amount; });
      else _producers.modify(pitr, 0, [&](auto &p) { p.blocks_per_cycle += amount; });
  }
}

void system_contract::set_producer_block_missed(account_name producer, uint32_t amount) {
  auto pitr = _producers.find(producer);
  if (pitr != _producers.end() && pitr->active()) {
    _producers.modify(pitr, 0, [&](auto &p) {
        p.missed_blocks += amount;
        if(crossed_missed_blocks_threshold(p.missed_blocks)) p.deactivate();
    });
  }
}

void system_contract::update_producer_blocks(account_name producer, uint32_t amountBlocksProduced, uint32_t amountBlocksMissed) {
  auto pitr = _producers.find(producer);
  if (pitr != _producers.end() && pitr->active()) {
      _producers.modify(pitr, 0, [&](auto &p) { 
        p.blocks_per_cycle += amountBlocksProduced; 
        p.missed_blocks += amountBlocksMissed;
        if(crossed_missed_blocks_threshold(p.missed_blocks)) p.deactivate();
      });
  }
}

void system_contract::check_missed_blocks(block_timestamp timestamp, account_name producer) {
    if(_grotations.current_bp == 0) {
        _grotations.last_time_block_produced = timestamp;
        _grotations.current_bp = producer;
        set_producer_block_produced(producer, 1);
        return;
    }
  
    //12 == 6s
    auto producedTimeDiff = timestamp.slot - _grotations.last_time_block_produced.slot;
    if(producedTimeDiff == 1 && producer == _grotations.current_bp) set_producer_block_produced(producer, producedTimeDiff); 
    else if(producedTimeDiff == 1 && producer != _grotations.current_bp) {
        //set zero to last producer blocks_per_cycle 
        set_producer_block_produced(_grotations.current_bp, RESET_BLOCKS_PRODUCED);
        //update current producer blocks_per_cycle 
        set_producer_block_produced(producer, producedTimeDiff);
    } 
    else if(producedTimeDiff > 1 && producer == _grotations.current_bp) update_producer_blocks(producer, 1, producedTimeDiff - 1);
    else {
        auto lastPitr = _producers.find(_grotations.current_bp);
        if (lastPitr == _producers.end()) return;
            
        account_name producers_schedule[21];
        uint32_t bytes_populated = get_active_producers(producers_schedule, sizeof(account_name)*21);
        
        auto currentProducerIndex = std::distance(producers_schedule, std::find(producers_schedule, producers_schedule + 21, producer));
        
        auto totalMissedSlots = std::fabs(producedTimeDiff - 1 - lastPitr->blocks_per_cycle);

        //last producer didn't miss blocks    
        if(totalMissedSlots == 0) {
            //set zero to last producer blocks_per_cycle 
            set_producer_block_produced(_grotations.current_bp, RESET_BLOCKS_PRODUCED);

            update_producer_blocks(producers_schedule[currentProducerIndex - 1], RESET_BLOCKS_PRODUCED, producedTimeDiff - 1);
            
            set_producer_block_produced(producer, 1);
        } else { //more than one producer missed blocks
            if(totalMissedSlots / MAX_BLOCK_PER_CYCLE > 0) {
                auto totalProdsMissedSlots = totalMissedSlots / 12;
                auto totalCurrentProdMissedBlocks = std::fmod(totalMissedSlots, 12);
                
                //Check if the last or the current bp missed blocks
                if(totalCurrentProdMissedBlocks > 0) {
                    auto lastProdTotalMissedBlocks = MAX_BLOCK_PER_CYCLE - lastPitr->blocks_per_cycle;
                    if(lastProdTotalMissedBlocks > 0) set_producer_block_missed(producers_schedule[currentProducerIndex - 1], lastProdTotalMissedBlocks);
                    
                    update_producer_blocks(producer, 1, totalCurrentProdMissedBlocks - lastProdTotalMissedBlocks);
                }  else set_producer_block_produced(producer, 1);
                
                for(int i = 0; i <= totalProdsMissedSlots; i++) {
                    auto lastProdIndex = currentProducerIndex - (i + 1);
                    lastProdIndex = lastProdIndex < 0 ? 21 + lastProdIndex : lastProdIndex;

                    auto prod = producers_schedule[lastProdIndex];
                    set_producer_block_missed(prod, MAX_BLOCK_PER_CYCLE);                       
                }

                set_producer_block_produced(_grotations.current_bp, RESET_BLOCKS_PRODUCED);
            } else {
                set_producer_block_produced(_grotations.current_bp, RESET_BLOCKS_PRODUCED);
                update_producer_blocks(producer, 1, totalMissedSlots);
            }
        }
    }    

    _grotations.last_time_block_produced = timestamp;
    _grotations.current_bp = producer;
}

void system_contract::onblock(block_timestamp timestamp, account_name producer) {
    require_auth(N(eosio));
    recalculate_votes();
    
    // Until activated stake crosses this threshold no new rewards are paid
    if (_gstate.total_activated_stake < min_activated_stake && _gstate.thresh_activated_stake_time == 0){
        return;
    }
     
    if (_gstate.last_pervote_bucket_fill == 0) /// start the presses
        _gstate.last_pervote_bucket_fill = current_time();

    /**
    * At startup the initial producer may not be one that is registered / elected
    * and therefore there may be no producer object for them.
    */
    auto prod = _producers.find(producer);
    if (prod != _producers.end()) {
        _gstate.total_unpaid_blocks++;
        _producers.modify(prod, 0, [&](auto &p) {
            p.unpaid_blocks++;
        });
    }

    check_missed_blocks(timestamp, producer);

    // Only update block producers once every minute, block_timestamp is in half seconds
    if (timestamp.slot - _gstate.last_producer_schedule_update.slot > 120) {

        update_elected_producers(timestamp);

        // Used in bidding for account names
        if ((timestamp.slot - _gstate.last_name_close.slot) > blocks_per_day)
        {
            name_bid_table bids(_self, _self);
            auto idx = bids.get_index<N(highbid)>();
            auto highest = idx.begin();

            if (highest != idx.end() &&
                highest->high_bid > 0 &&
                highest->last_bid_time < (current_time() - useconds_per_day) &&
                _gstate.thresh_activated_stake_time > 0 &&
                (current_time() - _gstate.thresh_activated_stake_time) > 14 * useconds_per_day)
            {
                _gstate.last_name_close = timestamp;
                idx.modify(highest, 0, [&](auto &b) {
                    b.high_bid = -b.high_bid;
                });
            }
        }
    }
}

void system_contract::recalculate_votes(){
    // fix type 1 : fixes just total vote weight
    // iterate and fix the total_producer_vote_weight = _producers(sum) if < 0 
    // for the current issue on the testnet : this should be removed once the fix is applied
    // if (_gstate.total_producer_vote_weight <= -0.1){ // -0.1 threshold for floating point calc ?
    //     print("\n Negative total_weight_vote fix applied !");
    //     _gstate.total_producer_vote_weight = 0;
    //     for (const auto &prod : _producers) {
    //         _gstate.total_producer_vote_weight += prod.total_votes;
    //     }
    // }

    // fix type 2 : fixes proxied weights too
    if (_gstate.total_producer_vote_weight <= -0.1){ // -0.1 threshold for floating point calc ?
        _gstate.total_producer_vote_weight = 0;
        _gstate.total_activated_stake = 0;
        for(auto producer = _producers.begin(); producer != _producers.end(); ++producer){
            _producers.modify(producer, 0, [&](auto &p) {
                p.total_votes = 0;
            });
        }
        boost::container::flat_map<account_name, bool> processed_proxies;
        for (auto voter = _voters.begin(); voter != _voters.end(); ++voter) {
            if(voter->proxy && !processed_proxies[voter->proxy]){
                auto proxy = _voters.find(voter->proxy);
                _voters.modify( proxy, 0, [&]( auto& av ) {
                    av.last_vote_weight = 0;
                    av.last_stake = 0;
                    av.proxied_vote_weight = 0;
                });
                processed_proxies[voter->proxy] = true;
            }
            if(!voter->is_proxy || !processed_proxies[voter->owner]){
                _voters.modify( voter, 0, [&]( auto& av ) {
                    av.last_vote_weight = 0;
                    av.last_stake = 0;
                    av.proxied_vote_weight = 0;
                });
                processed_proxies[voter->owner] = true;
            }
            update_votes(voter->owner, voter->proxy, voter->producers, true);
        }
    }
}

/**
 * RATIONALE: Minimum Unpaid Blocks Threshold
 *
 * In the Telos Payment Architecture, block reward payments are calculated on the fly at the time
 * of the call to claimrewards. When called, the claimrewards function determines which payment level
 * each producer qualifies for and saves their payment to the payments table. Payments are then doled
 * out over the course of a day to the intended recipient.
 *
 * In order to qualify for a Producer level payout, the caller must be in the top 21 producers AND have
 * at least 12 hours worth of block production as a producer.
 */
void system_contract::claimrewards(const account_name &owner) {
    require_auth(owner);

    const auto &prod = _producers.get(owner);
    eosio_assert(prod.active(), "producer does not have an active key");

    eosio_assert(_gstate.total_activated_stake >= min_activated_stake,
                 "cannot claim rewards until the chain is activated (at least 15% of all tokens participate in voting)");

    auto ct = current_time();
    eosio_assert(ct - prod.last_claim_time > useconds_per_day, "already claimed rewards within past day");

    const asset token_supply = token(N(eosio.token)).get_supply(symbol_type(system_token_symbol).name());
    const auto usecs_since_last_fill = ct - _gstate.last_pervote_bucket_fill;

    if (usecs_since_last_fill > 0 && _gstate.last_pervote_bucket_fill > 0)
    {
        auto new_tokens = static_cast<int64_t>((continuous_rate * double(token_supply.amount) * double(usecs_since_last_fill)) / double(useconds_per_year));

        auto to_producers = (new_tokens / 5) * 2;
        auto to_workers = new_tokens - to_producers;

        INLINE_ACTION_SENDER(eosio::token, issue)
        (N(eosio.token), {{N(eosio), N(active)}}, {N(eosio), asset(new_tokens), "Issue new TLOS tokens"});

        INLINE_ACTION_SENDER(eosio::token, transfer)
        (N(eosio.token), {N(eosio), N(active)}, {N(eosio), N(eosio.saving), asset(to_workers), "Transfer worker proposal share to eosio.saving account"});

        INLINE_ACTION_SENDER(eosio::token, transfer)
        (N(eosio.token), {N(eosio), N(active)}, {N(eosio), N(eosio.bpay), asset(to_producers), "Transfer producer share to per-block bucket"});

        _gstate.perblock_bucket += to_producers;
        _gstate.last_pervote_bucket_fill = ct;

        //print("\nMinted Tokens: ", new_tokens);
        //print("\n   >Worker Fund: ", to_workers);
        //print("\n   >Producer Fund: ", to_producers);
    }

    //Sort _producers table
    auto sortedProds = _producers.get_index<N(prototalvote)>();

    uint32_t count = 0;
    uint32_t index = 0;

    // NOTE: Loop stops after 51st iteration. Counting farther than 51 is unnecessary when calculating payouts.
    for (const auto &item : sortedProds)
    {
        if (item.active()) { //Only count activated producers
            auto prodName = name{item.owner};
	        count++;

            if (owner == item.owner) {
                index = count;
                print("\nProducer Found: ", prodName);
                //print("\nIndex: ", index);
            }

            if (count >= 51) {
                break;
            }
        }
    }

    //print("\nTotal Count = ", count);

    uint32_t numProds = 0;
    uint32_t numStandbys = 0;
    int64_t totalShares = 0;

    // Calculate totalShares
    // TEST: Extensive testing to ensure no precision is lost during calculation of claimed rewards.
    if (count <= 21)
    {
        totalShares = (count * uint32_t(2));
        numProds = count;
        numStandbys = 0;
    } else {
        totalShares = (count + 21);
        numProds = 21;
        numStandbys = (count - 21);
    }

    //print("\nnumProds: ", numProds);
    //print("\nnumStandbys: ", numStandbys);
    //print("\n_gstate.perblock_bucket: ", asset(_gstate.perblock_bucket));
    //print("\ntotalShares: ", totalShares);

    auto shareValue = (_gstate.perblock_bucket / totalShares);
    //print("\nshareValue: ", asset(shareValue));

    int64_t pay_amount = 0;

    /**
     * RATIONALE: Minimum Unpaid Blocks Threshold
     *
     * In the Telos Payment Architecture, block reward payments are calculated on the fly at the time
     * of the call to claimrewards. When called, the claimrewards function determines which payment level
     * the calling account qualifies for and pays them accordingly.
     *
     * In order to qualify for a Producer level payout, the caller must be in the top 21 producers AND have
     * at least 12 hours worth of block production as a producer. Requiring the calling account to have produced
     * a minimum of 12 hours worth of blocks ensures Standby's can't "jump the fence" just long enough to call
     * claimrewards and take a producer's share of the payout.
     */

    // Determine if an account is a Producer or Standby, and calculate shares accordingly
    if (_gstate.total_unpaid_blocks > 0)
    {
        if (index <= 21 && prod.unpaid_blocks >= min_unpaid_blocks_threshold) {
            pay_amount = (shareValue * int64_t(2));
            print("\nCaller is a Producer @ ", index);
            print("\nUnpaid blocks: ", prod.unpaid_blocks);
            print("\nPayment: ", asset(pay_amount));
        } else if (index <= 21 && prod.unpaid_blocks <= min_unpaid_blocks_threshold) {
            pay_amount = shareValue;
            print("\nCaller is a Producer @ ", index);
            print("\nUnpaid blocks: ", prod.unpaid_blocks);
            print("\nCaller doesn't meet minimum unpaid blocks threshold of: ", min_unpaid_blocks_threshold);
            print("\nPayment: ", asset(pay_amount));
        } else if (index >= 22 && index <= 51) {
            pay_amount = shareValue;
            print("\nCaller is a Standby @ ", index);
            print("\nUnpaid blocks: ", prod.unpaid_blocks);
            print("\nPayment: ", asset(pay_amount));
        } else {
            pay_amount = 0;
            print("\nCaller is outside Top 51 @ ", index);
            print("\nUnpaid blocks: ", prod.unpaid_blocks);
            print("\nPayment: ", asset(pay_amount));
        }
    }

    /**
     * TODO: Implement Missed Block Deductions
     *
     * Telos Payout Architecture will account for missed blocks, and reduce
     * payout based on percentage of missed blocks.
     *
     * For instance, if Producer A misses 3 of their 12 blocks, they will
     * have missed 25% of their scheduled blocks. For easy math, say producers
     * earn 1 TLOS per block. This means Producer A will receive a payment of
     * 9 TLOS for their work, but the other 3 will remain in the bucket.
     */

    _gstate.perblock_bucket -= pay_amount;
    _gstate.total_unpaid_blocks -= prod.unpaid_blocks;

    _producers.modify(prod, 0, [&](auto &p) {
        p.last_claim_time = ct;
        p.unpaid_blocks = 0;
    });

    if (pay_amount > 0)
    {
        INLINE_ACTION_SENDER(eosio::token, transfer)
        (N(eosio.token), {N(eosio.bpay), N(active)}, {N(eosio.bpay), owner, asset(pay_amount), std::string("Producer/Standby Payment")});
    }
}

} //namespace eosiosystem