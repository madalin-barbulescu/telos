#include "trail.service.hpp"

trail::trail(account_name self) : contract(self), env_singleton(self, self) {
    if (!env_singleton.exists()) {

        env_struct = environment{
            self, //publisher
            0, //total_tokens
            0 //total_voters
        };

        env_singleton.set(env_struct, self);
    } else {
        env_struct = env_singleton.get();
    }
}

trail::~trail() {
    if (env_singleton.exists()) {
        env_singleton.set(env_struct, env_struct.publisher);
    }
}

void trail::regtoken(asset native, account_name publisher) {
    require_auth(publisher);

    auto sym = native.symbol.name();
    registries_table registries(_self, sym);
    auto r = registries.find(sym);

    eosio_assert(r == registries.end(), "Token Registry with that symbol already exists.");

    registries.emplace(publisher, [&]( auto& a ){
        a.native = native;
        a.publisher = publisher;
    });

    env_struct.total_tokens++;

    print("\nToken Registration: SUCCESS");
}

void trail::unregtoken(asset native, account_name publisher) {
    require_auth(publisher);
    
    auto sym = native.symbol.name();
    registries_table registries(_self, sym);
    auto r = registries.find(sym);

    eosio_assert(r != registries.end(), "Token Registry does not exist.");

    registries.erase(r);

    env_struct.total_tokens--;

    print("\nToken Unregistration: SUCCESS");
}

void trail::regvoter(account_name voter) {
    require_auth(voter);

    voters_table voters(_self, voter);
    auto v = voters.find(voter);

    eosio_assert(v == voters.end(), "Voter already exists");

    voters.emplace(voter, [&]( auto& a ){
        a.voter = voter;
    });

    env_struct.total_voters++;

    print("\nVoterID Registration: SUCCESS");
}

void trail::unregvoter(account_name voter) {
    require_auth(voter);

    voters_table voters(_self, voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "Voter Doesn't Exist");

    auto vid = *v;

    eosio_assert(vid.receipt_list.size() == 0, "Can't delete until all vote receipts removed");    

    voters.erase(v);

    env_struct.total_voters--;

    print("\nVoterID Unregistration: SUCCESS");
}

void trail::addreceipt(uint64_t vote_code, uint64_t vote_scope, uint64_t vote_key, uint16_t direction, account_name voter) {
    //NOTE: Maybe require_auth2? Call should only come from voting contract
    require_auth(voter);

    voters_table voters(_self, voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "Voter doesn't exist");

    auto vid = *v;
    int64_t new_weight = get_liquid_tlos(voter);

    votereceipt new_vr = votereceipt{
        vote_code,
        vote_scope,
        vote_key,
        direction,
        new_weight
    };

    auto itr = vid.receipt_list.begin();

    //search for existing receipt
    for (votereceipt r : vid.receipt_list) {
        if (r.vote_code == vote_code && r.vote_scope == vote_scope && r.vote_key == vote_key) {
            print("\nExisting receipt found. Updating...");

            auto h = vid.receipt_list.erase(itr);
            auto i = vid.receipt_list.insert(itr, new_vr);

            break;
        }
        itr++;
    }

    if (itr == vid.receipt_list.end()) {
        vid.receipt_list.push_back(new_vr);
    } 

    voters.modify(v, 0, [&]( auto& a ) {
        a.receipt_list = vid.receipt_list;
    });

    print("\nVoteReceipt Addition: SUCCESS");
}

void trail::rmvreceipt(uint64_t vote_code, uint64_t vote_scope, uint64_t vote_key, account_name voter) {
    require_auth(voter);

    voters_table voters(_self, voter);
    auto v = voters.find(voter);

    eosio_assert(v != voters.end(), "Voter doesn't exist");

    auto vid = *v;

    auto itr = vid.receipt_list.begin();
    bool found = false;

    //search for existing receipt
    for (votereceipt r : vid.receipt_list) {

        if (r.vote_code == vote_code && r.vote_scope == vote_scope && r.vote_key == vote_key) {
            print("\nExisting receipt found. Removing...");
            found = true;

            auto vr = vid.receipt_list.erase(itr);

            break;
        }

        itr++;
    }

    eosio_assert(found, "Receipt not found");

    voters.modify(v, 0, [&]( auto& a ) {
        a.receipt_list = vid.receipt_list;
    });

    print("\nVoteReceipt Removal: SUCCESS");
}

EOSIO_ABI(trail, (regtoken)(unregtoken)(regvoter)(unregvoter)(addreceipt)(rmvreceipt))