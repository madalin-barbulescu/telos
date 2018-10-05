#include "ratify.amend.hpp"

#include <eosiolib/print.hpp>

ratifyamend::ratifyamend(account_name self) : contract(self), thresh_singleton(self, self) {
    if (!thresh_singleton.exists()) {

        thresh_struct = threshold{
            _self, //publisher
            0, //initial total_voters
            0, //initial quorum_threshold
            uint32_t(600) //expiration_length in seconds (default is 5,000,000 or ~58 days)
        };

        update_thresh();
        thresh_singleton.set(thresh_struct, _self);
    } else {

        thresh_struct = thresh_singleton.get();     
        update_thresh();
        thresh_singleton.set(thresh_struct, _self);
    }
}

ratifyamend::~ratifyamend() {
    if (thresh_singleton.exists()) {
        thresh_singleton.set(thresh_struct, _self);
    }
}

void ratifyamend::insertdoc(string title, vector<string> clauses) {
    require_auth(_self); //only contract owner can insert new document
    
    documents_table documents(_self, _self);

    uint64_t doc_id = documents.available_primary_key();

    documents.emplace(_self, [&]( auto& a ){
        a.id = doc_id;
        a.title = title;
        a.clauses = clauses;
    });

    print("\nDocument Insertion: SUCCESS");
    print("\nAssigned Document ID: ", doc_id);
}

void ratifyamend::propose(string title, uint64_t document_id, vector<uint16_t> new_clause_ids, vector<string> new_ipfs_urls, account_name proposer) {
    require_auth(proposer);

    documents_table documents(_self, _self);
    auto d = documents.find(document_id);

    eosio_assert(d != documents.end(), "Document Not Found");
    auto doc_struct = *d;

    eosio_assert(new_clause_ids.size() == new_ipfs_urls.size(), "Clause ID vector and IPFS url vector sizes must match");

    auto doc_size = doc_struct.clauses.size();
    int16_t last_clause_id = -1;
    auto last_ipfs_url = 0;

    for (int i = 0; i < new_clause_ids.size(); i++) {
        eosio_assert(new_clause_ids.at(i) > last_clause_id, "Clause IDs Must Be In Ascending Order");
        last_clause_id = new_clause_ids.at(i);
        eosio_assert(new_clause_ids.at(i) <= (doc_size + 1), "Invalid Clause Number");
        
        last_ipfs_url++;
        if (new_clause_ids.at(i) == (doc_size + 1)) { //if clause is new, increase doc_size
            doc_size++;
        }
    }

    //NOTE: 100.0000 TLOS fee, refunded if proposal passes
    action(permission_level{ proposer, N(active) }, N(eosio.token), N(transfer), make_tuple( //NOTE: susceptible to ram-drain bug
    	proposer,
        _self,
        asset(int64_t(1000000), S(4, TLOS)),
        std::string("Ratify/Amend Proposal Fee")
	)).send();

    proposals_table proposals(_self, _self);

    proposals.emplace(proposer, [&]( auto& a ){
        a.id = proposals.available_primary_key();
        a.document_id = document_id;
        a.title = title;
        a.new_clause_ids = new_clause_ids;
        a.new_ipfs_urls = new_ipfs_urls;
        a.yes_count = 0;
        a.no_count = 0;
        a.abstain_count = 0;
        a.proposer = proposer;
        a.expiration = now() + thresh_struct.expiration_length;
        a.status = 0;
    });
}

void ratifyamend::vote(uint64_t proposal_id, uint16_t direction, account_name voter) {
    require_auth(voter);
    eosio_assert(direction >= 0 && direction <= 2, "Invalid Vote. [0 = NO, 1 = YES, 2 = ABSTAIN]");

    voters_table voters(N(trailservice), voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "VoterID Not Found");
    
    print("\nVoterID Found");
    auto vid = *v;

    proposals_table proposals(_self, _self);
    auto p = proposals.find(proposal_id);
    eosio_assert(p != proposals.end(), "Proposal Not Found");
    
    print("\nProposal Found");
    auto prop = *p;

    eosio_assert(prop.expiration > now(), "Proposal Has Expired");

    if (vid.receipt_list.empty()) {

        print("\nReceipt List Empty...Calling TrailService to update VoterID");

        action(permission_level{ voter, N(active) }, N(trailservice), N(addreceipt), make_tuple(
    	    _self,      
    	    _self,
    	    prop.id,
            direction,
            voter
	    )).send();

        print("\nReceipt Added. VoterID Successfully Updated");
    } else {

        print("\nSearching receipts list for existing VoteReceipt...");

        bool found = false;

        for (votereceipt r : vid.receipt_list) {
            if (r.vote_key == proposal_id) {

                print("\nVoteReceipt receipt found");
                found = true;

                switch (r.direction) {
                    case 0 : prop.no_count = (prop.no_count - uint64_t(r.weight)); break;
                    case 1 : prop.yes_count = (prop.yes_count - uint64_t(r.weight)); break;
                    case 2 : prop.abstain_count = (prop.abstain_count - uint64_t(r.weight)); break;
                }

                print("\nCalling TrailService to update VoterID...");

                action(permission_level{ voter, N(active) }, N(trailservice), N(addreceipt), make_tuple(
    	            _self,      
    	            _self,
    	            prop.id,
                    direction,
                    voter
	            )).send();

                print("\nVoterID Successfully Updated");

                break;
            }
        }

        if (found == false) {
            print("\nVoteInfo not found in list. Calling TrailService to insert...");

            action(permission_level{ voter, N(active) }, N(trailservice), N(addreceipt), make_tuple(
    	        _self,      
    	        _self,
    	        prop.id,
                direction,
                voter
	        )).send();

            print("\nVoterID Successfully Updated");
        }
    }

    string vote_type;
    int64_t new_weight = get_liquid_tlos(voter);

    switch (direction) {
        case 0 : prop.no_count = (prop.no_count + uint64_t(new_weight)); vote_type = "NO"; break;
        case 1 : prop.yes_count = (prop.yes_count + uint64_t(new_weight)); vote_type = "YES"; break;
        case 2 : prop.abstain_count = (prop.abstain_count + uint64_t(new_weight)); vote_type = "ABSTAIN"; break;
    }

    proposals.modify(p, 0, [&]( auto& a ) {
        a.no_count = prop.no_count;
        a.yes_count = prop.yes_count;
        a.abstain_count = prop.abstain_count;
    });

    print("\n\nVote: SUCCESSFUL");
    print("\nYour Vote: ", vote_type);
    print("\nVote Weight: ", new_weight);
}

void ratifyamend::unvote(uint64_t proposal_id, account_name voter) {
    voters_table voters(N(trailservice), voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "VoterID Not Found");
    
    print("\nVoterID Found");
    auto vid = *v;

    eosio_assert(vid.receipt_list.size() > 0, "No votes in list to unvote");

    proposals_table proposals(_self, _self);
    auto p = proposals.find(proposal_id);
    eosio_assert(p != proposals.end(), "Proposal Not Found");
    
    print("\nProposal Found");
    auto prop = *p;

    auto itr = vid.receipt_list.begin();
    int64_t weight = 0;
    uint16_t direction;

    for (votereceipt r : vid.receipt_list) {
        if (r.vote_key == proposal_id) {
            weight = r.weight;
            direction = r.direction;

            print("\nReceipt removed");

            break;
        }

        itr++;
    }

    eosio_assert(itr != vid.receipt_list.end(), "Receipt doesn't exist");

    if (prop.status == 0) {

        switch (direction) {
            case 0 : prop.no_count = (prop.no_count - uint64_t(weight)); break;
            case 1 : prop.yes_count = (prop.yes_count - uint64_t(weight)); break;
            case 2 : prop.abstain_count = (prop.abstain_count - uint64_t(weight)); break;
        }

        proposals.modify(p, 0, [&]( auto& a ) {
            a.no_count = prop.no_count;
            a.yes_count = prop.yes_count;
            a.abstain_count = prop.abstain_count;
        });
    }

    action(permission_level{ voter, N(active) }, N(trailservice), N(rmvreceipt), make_tuple(
    	_self,      
    	_self,
    	prop.id,
        voter
	)).send();
}

void ratifyamend::close(uint64_t proposal_id) { //TODO: add require_auth for proposer?
    proposals_table proposals(_self, _self);
    auto p = proposals.find(proposal_id);

    eosio_assert(p != proposals.end(), "Proposal Not Found");
    auto po = *p;

    eosio_assert(po.expiration <= now(), "Voting Window Still Open");
    eosio_assert(po.status == 0, "Proposal Already Closed");

    uint64_t total_votes = (po.yes_count + po.no_count + po.abstain_count); //total votes cast on proposal
    uint64_t pass_thresh = ((po.yes_count + po.no_count) / 3) * 2; // 66.67% of total votes

    //refund thresholds
    uint64_t q_refund_thresh = thresh_struct.total_voters / 25; //4% of all registered voters
    uint64_t p_refund_thresh = total_votes / 4; //25% of votes cast

    //check for zero votes
    if (total_votes == uint64_t(0)) {

        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 2;
        });

        print("\nProposal Failed With 0 Votes.");

        return;
    }

    //determine pass/fail and refund eligibility
    if (total_votes >= thresh_struct.quorum_threshold && po.yes_count >= pass_thresh) { //PASS

        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 1;
        });

        action(permission_level{ _self, N(active) }, N(eosio.token), N(transfer), make_tuple( //NOTE: susceptible to ram-drain bug
    	    _self,
            po.proposer,
            asset(int64_t(1000000), S(4, TLOS)),
            std::string("Ratify/Amend Proposal Fee Refund")
	    )).send();

        update_doc(po.document_id, po.new_clause_ids, po.new_ipfs_urls);

        print("\nProposal Passed...Refund Sent...Documents Updated.");

    } else if (po.yes_count >= p_refund_thresh && total_votes >= q_refund_thresh) { //FAILED, REFUND CHECK
        
        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 2;
        });

        action(permission_level{ _self, N(active) }, N(eosio.token), N(transfer), make_tuple( //NOTE: susceptible to ram-drain bug
    	    _self,
            po.proposer,
            asset(int64_t(1000000), S(4, TLOS)),
            std::string("Ratify/Amend Proposal Fee Refund")
	    )).send();

        print("\nProposal Failed...Refund Sent.");
        
    } else { //FAILED, NO REFUND CHECK

        proposals.modify(p, 0, [&]( auto& a ) {
            a.status = 2;
        });

        print("\nProposal Passed.");
    }
    
}

void ratifyamend::update_thresh() {

    environment_singleton env(N(trailservice), N(trailservice));
    environment e = env.get();

    uint64_t new_quorum = e.total_voters / 4; //25% of all registered voters
    uint64_t new_total_voters = e.total_voters;

    thresh_struct.quorum_threshold = new_quorum;
    thresh_struct.total_voters = new_total_voters;
}

void ratifyamend::update_doc(uint64_t document_id, vector<uint16_t> new_clause_ids, vector<string> new_ipfs_urls) {
    documents_table documents(_self, _self);
    auto d = documents.find(document_id);
    auto doc = *d;

    auto doc_size = doc.clauses.size();
    for (int i = 0; i < new_clause_ids.size(); i++) {
        
        if (new_clause_ids[i] < doc.clauses.size()) { //update existing clause
            doc.clauses[new_clause_ids[i]] = new_ipfs_urls.at(i);
        } else { //add new clause
            doc.clauses.push_back(new_ipfs_urls.at(i));
        }
    }

    documents.modify(d, 0, [&]( auto& a ) {
        a.clauses = doc.clauses;
    });
}

EOSIO_ABI( ratifyamend, (insertdoc)(propose)(vote)(unvote)(close))