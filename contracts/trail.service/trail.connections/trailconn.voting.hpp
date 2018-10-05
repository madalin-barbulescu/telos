/**
 * This file includes all definitions necessary to interact with Trail's voting system. Developers who want to
 * utilize the system simply must include this file in their implementation to interact with the information
 * stores by Trail.
 * 
 * @author Craig Branscom
 */

#include <eosiolib/eosio.hpp>
#include <eosiolib/permission.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/singleton.hpp>

using namespace std;
using namespace eosio;

/**
 * @brief Generic struct used to log a vote.
 * @field vote_code - code of contract where vote was cast
 * @field vote_scope - scope of table where vote was cast
 * @field vote_key - primary key of object where vote is stored
 * @field direction - direction of vote, where 0 = NO, 1 = YES, and 2 = ABSTAIN
 * @field weight - weight of vote
 * 
 * TODO: Could this be EOSLIB_SERIALIZED?
 */
struct votereceipt {
    uint64_t vote_code; //TODO: change to account_name type?
    uint64_t vote_scope;
    uint64_t vote_key;
    uint16_t direction; //TODO: Use enum?
    int64_t weight;
};

/**
 * @brief A VoterID card, used to track and store voter information and activity.
 * @field voter - account name of voter who owns the ID card
 * @field receipt_list - vector of all votereceipts
 */
/// @abi table voters i64
struct voterid {
    account_name voter;
    vector<votereceipt> receipt_list;

    uint64_t primary_key() const { return voter; }
    EOSLIB_SERIALIZE(voterid, (voter)(receipt_list))
};

/**
 * @brief Environment struct tracking global trailservice information.
 * @field publisher - account that published the contract.
 * @field total_tokens - total active number of unique tokens registered through the regtoken() action.
 * @field total_voters - total active number of unique voterid's registered through the regvoter() action.
 */
/// @abi table environment i64
struct environment {
    account_name publisher;
    uint64_t total_tokens;
    uint64_t total_voters;

    uint64_t primary_key() const { return publisher; }
    EOSLIB_SERIALIZE(environment, (publisher)(total_tokens)(total_voters))
};

typedef multi_index<N(voters), voterid> voters_table;
typedef singleton<N(environment), environment> environment_singleton;

//----------EOSIO.TOKEN DEFINITIONS----------

struct account {
    asset balance;

    uint64_t primary_key()const { return balance.symbol.name(); }
};

typedef eosio::multi_index<N(accounts), account> accounts;

/**
 * Updates the voter's tlos_weight on their VoterID.
 * @param voter - account from which to retrieve liquid TLOS amount
*/
int64_t get_liquid_tlos(account_name voter) {
    accounts accountstable(N(eosio.token), voter);
    auto a = accountstable.find(asset(int64_t(0), S(4, TLOS)).symbol.name()); //TODO: find better way to get TLOS symbol?

    int64_t liquid_tlos = 0;

    if (a != accountstable.end()) {
        auto acct = *a;
        liquid_tlos = acct.balance.amount / int64_t(10000); //divide to get actual balance
    }
    
    return liquid_tlos;
}