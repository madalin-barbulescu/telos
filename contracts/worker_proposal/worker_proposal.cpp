#include "worker_proposal.hpp"
#include <eosiolib/print.hpp>

using namespace eosio;
using eosio::print;

workerproposal::workerproposal(account_name self) : contract(self), wp_env_singleton(self, self) {
    wp_env_struct = wp_env_singleton.get_or_create(_self, 
        wp_env{
            _self, //publisher
            0, //initial total_voters
            0, //initial quorum_threshold
            2500000, // cycle duration in seconds (default 2,500,000 or 5,000,000 blocks or ~29 days)
            3, // percent from requested amount (default 3%)
            500000 // minimum fee amount (default 50 TLOS)
        }
    );
    update_env();
}

workerproposal::~workerproposal() {
    wp_env_singleton.set(wp_env_struct, _self);
}

void workerproposal::submission(account_name proposer, std::string title, std::string text, uint16_t cycles, std::string ipfs_location, asset amount, account_name send_to) {
    require_auth(proposer);

    // calc fee
    uint64_t fee_amount = uint64_t(amount.amount) * uint64_t( wp_env_struct.fee_percentage ) / uint64_t(100);
    fee_amount = fee_amount > wp_env_struct.fee_min ? fee_amount :  wp_env_struct.fee_min;

    // transfer the fee
    action(permission_level{ proposer, N(active) }, N(eosio.token), N(transfer), make_tuple(
    	proposer,
        N(eosio.saving),
        asset(fee_amount, S(4, TLOS)),
        std::string("Worker Proposal Fee")
	)).send();

    proposals _proptable(_self, _self);
    
    _proptable.emplace( proposer, [&]( proposal& info ){
        info.id                  = _proptable.available_primary_key();
        info.title               = title;
        info.ipfs_location       = ipfs_location;
        info.text_hash           = text;
        info.cycles              = cycles;
        info.amount              = uint64_t(amount.amount);
        info.send_to             = send_to;
        info.fee                 = fee_amount;
        info.proposer            = proposer;
        info.yes_count           = 0;
        info.no_count            = 0;
        info.last_cycle_check    = 0;
        info.last_cycle_status   = false;
        info.created             = now();
        info.outstanding         = uint64_t(0);
    });
}

void workerproposal::vote(uint64_t proposal_id, uint16_t direction, account_name voter) {
    require_auth(voter);

    eosio_assert(direction >= 0 && direction <= 2, "Invalid Vote. [0 = NO, 1 = YES, 2 = ABSTAIN]");

    voters_table voters(N(trailservice), voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "VoterID Not Found");
    
    auto vid = *v;

    proposals proptable(_self, _self);
    auto p = proptable.find(proposal_id);
    eosio_assert(p != proptable.end(), "Proposal Not Found");
    
    auto prop = *p;

    eosio_assert(prop.created + prop.cycles * wp_env_struct.cycle_duration > now(), "Proposal Has Expired");

    if (vid.receipt_list.empty()) {

        action(permission_level{ voter, N(active) }, N(trailservice), N(addreceipt), make_tuple(
    	    _self,      
    	    _self,
    	    prop.id,
            direction,
            prop.created + prop.cycles * wp_env_struct.cycle_duration,
            voter
	    )).send();

    } else {

        bool found = false;

        for (votereceipt r : vid.receipt_list) {
            if (r.vote_key == proposal_id) {

                found = true;

                switch (r.direction) {
                    case 0 : prop.no_count = (prop.no_count - 1); break; // removed uint64_t(r.weight)
                    case 1 : prop.yes_count = (prop.yes_count - 1); break; // removed uint64_t(r.weight)
                    case 2 : prop.abstain_count = (prop.abstain_count - 1); break; // removed uint64_t(r.weight)
                }


                action(permission_level{ voter, N(active) }, N(trailservice), N(addreceipt), make_tuple(
    	            _self,      
    	            _self,
    	            prop.id,
                    direction,
                    prop.created + prop.cycles * wp_env_struct.cycle_duration,
                    voter
	            )).send();


                break;
            }
        }

        if (found == false) {
            action(permission_level{ voter, N(active) }, N(trailservice), N(addreceipt), make_tuple(
    	        _self,      
    	        _self,
    	        prop.id,
                direction,
                prop.created + prop.cycles * wp_env_struct.cycle_duration,
                voter
	        )).send();
        }

    }

    int64_t new_weight = get_liquid_tlos(voter);

    switch (direction) {
        case 0 : prop.no_count = (prop.no_count + 1); break; // removed uint64_t(new_weight); TO ADD after trails.service refactor
        case 1 : prop.yes_count = (prop.yes_count + 1); break; // removed uint64_t(new_weight); TO ADD after trails.service refactor
        case 2 : prop.abstain_count = (prop.abstain_count + 1); break; // removed uint64_t(new_weight); TO ADD after trails.service refactor
    }

    update_outstanding(prop);
    
    proptable.modify(p, 0, [&]( auto& a ) {
        a.no_count = prop.no_count;
        a.yes_count = prop.yes_count;
        a.abstain_count = prop.abstain_count;
        a.outstanding = prop.outstanding;
        a.last_cycle_check = prop.last_cycle_check;
        a.last_cycle_status = prop.last_cycle_status;
        a.fee = prop.fee;
    });
}

void workerproposal::claim(uint64_t proposal_id) {
    proposals proptable(_self, _self);
    auto p = proptable.find(proposal_id);

    eosio_assert(p != proptable.end(), "Proposal Not Found");
    auto po = *p;

    require_auth(po.proposer);

    eosio_assert(po.last_cycle_check < po.cycles, "Already claimed all funds");
    
    update_outstanding(po);
    if(po.outstanding) {

        action(permission_level{ _self, N(active) }, N(eosio.token), N(transfer), make_tuple(
    	    N(eosio.saving),
            po.proposer,
            asset(po.outstanding, S(4, TLOS)),
            std::string("Worker Proposal Funds")
	    )).send();

        po.outstanding = 0;

    }

    proptable.modify(p, 0, [&]( auto& a ) {
        a.fee = po.fee;
        a.outstanding = po.outstanding;
        a.last_cycle_check = po.last_cycle_check;
        a.last_cycle_status = po.last_cycle_status;
    }); 
}

bool workerproposal::check_fee_threshold(proposal p) {

    //      1. min 20% YES votes
    //      2. 0.1% of all TLOS tokens voting at the conclusion of voting
            // 5% of voters for test (TO CHANGE)

    uint64_t total_votes = (p.yes_count + p.no_count + p.abstain_count); //total votes cast on proposal
    uint64_t q_fee_refund_thresh = wp_env_struct.total_voters / 20; //0.1% of all registered voters // 10% of voters for test (TO CHANGE)
    uint64_t p_fee_refund_thresh = total_votes / 5; //20% of total votes

    return p.yes_count > 0
        && p.yes_count >= p_fee_refund_thresh
        && total_votes >= q_fee_refund_thresh;
}

bool workerproposal::check_threshold(proposal p) {

    //      1. >50% YES votes over NO votes
    //      2.  5.0% of votes from all votable TLOS tokens at the conclusion of the Voting Period
                // 10% of voters for test (TO CHANGE)

    uint64_t total_votes = (p.yes_count + p.no_count + p.abstain_count ); //total votes cast on proposal    

    return p.yes_count > p.no_count
            && total_votes >= wp_env_struct.total_voters / 10;
}

void workerproposal::update_outstanding(proposal &p) {
    
    uint64_t current_cycle = (now() - p.created) / wp_env_struct.cycle_duration;
    
    if(current_cycle > p.cycles) {
        current_cycle = p.cycles;
    }

    if(current_cycle > p.last_cycle_check && p.last_cycle_status) {
        p.outstanding += p.amount *  (current_cycle - p.last_cycle_check);
    }

    if(current_cycle > 0 && p.fee && check_fee_threshold(p)) {
        p.outstanding += p.fee;
        p.fee = 0;
    }
    
    p.last_cycle_check = current_cycle;
    p.last_cycle_status = check_threshold(p);
}

void workerproposal::update_env() {

    environment_singleton env(N(trailservice), N(trailservice));
    environment e = env.get();

    uint64_t new_quorum = e.total_voters / 20; // 5% of all registered voters
    uint64_t new_total_voters = e.total_voters;

    wp_env_struct.quorum_threshold = new_quorum;
    wp_env_struct.total_voters = new_total_voters;
}


EOSIO_ABI( workerproposal, (submission)(vote)(claim))