/**
 * The ratify/amend contract is used to submit proposals for ratifying and amending major documents in the Telos Network. Users register their
 * account through the Trail Service, where they will be issued a VoterID card that tracks and stores their vote participation. Once registered,
 * users can then cast votes equal to their weight of liquid TLOS on proposals to update individual clauses within a document. Submitting a 
 * proposal requires a deposit of 100.0000 TLOS. Deposit will be refunded if proposal reaches a minimum threshold of participation and acceptance
 * by registered voters (even if the proposal itself fails to pass).
 * 
 * @author Craig Branscom
 * @copyright defined in telos/LICENSE.txt
 */

#include <../trail.service/trail.connections/trailconn.voting.hpp> //Import trailservice voting data definitions

#include <eosiolib/eosio.hpp>
#include <eosiolib/permission.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/singleton.hpp>

using namespace std;
using namespace eosio;

class ratifyamend : public contract {
    public:

        ratifyamend(account_name self);

        ~ratifyamend();

        /**
         * Creates a new document and inserts it into the documents table.
         * @param title - string representation of document title
         * @param clauses - vector of strings representing ipfs urls to the text of each clause
         */
        void insertdoc(string title, vector<string> clauses);

        /**
         * Creates a new proposal and inserts it into the proposals table. Overwrites an existing clause.
         * @param title - string representation of proposal title
         * @param ipfs_url - ipfs url of proposal text
         * @param document_id - id of document being edited
         * @param clause_id - id of clause in document to overwrite
         * @param proposer - account name submitting proposal
         */
        void propose(string title, uint64_t document_id, vector<uint16_t> new_clause_ids, vector<string> new_ipfs_urls, account_name proposer);

        /**
         * Casts a vote in a certain direction on a proposal, by the weight of the voter's tlos tokens.
         * @param proposal_id - id of proposal to vote on
         * @param vote - direction of vote (0 = NO, 1 = YES, 2 = ABSTAIN)
         */
        void vote(uint64_t proposal_id, uint16_t vote, account_name voter);

        /**
         * Removes a vote from the proposal matching the given proposal id.
         * @param proposal_id - id of proposal from which to rescinf vote
         * @param voter - account name that cast vote being removed
         */
        void unvote(uint64_t proposal_id, account_name voter);

        /**
         * Closes a proposal and declares a PASS or FAIL if its past its expiration, and not already closed.
         * @param proposal_id - proposal to close
         */
        void close(uint64_t proposal_id);

    protected:

        void update_thresh();

        void update_doc(uint64_t document_id, vector<uint16_t> new_clause_ids, vector<string> new_ipfs_urls);

        /// @abi table documents i64
        struct document {
            uint64_t id;
            string title;
            vector<string> clauses; //vector of ipfs urls

            uint64_t primary_key() const { return id; }
            EOSLIB_SERIALIZE(document, (id)(title)(clauses))
        };

        /// @abi table proposals i64
        struct proposal {
            uint64_t id;
            uint64_t document_id;
            string title;
            vector<uint16_t> new_clause_ids;
            vector<string> new_ipfs_urls;
            uint64_t yes_count;
            uint64_t no_count;
            uint64_t abstain_count;
            account_name proposer;
            uint32_t expiration;
            uint64_t status; // 0 = OPEN, 1 = PASSED, 2 = FAILED

            uint64_t primary_key() const { return id; }
            EOSLIB_SERIALIZE(proposal, (id)(document_id)(title)(new_clause_ids)(new_ipfs_urls)(yes_count)(no_count)(abstain_count)(proposer)(expiration)(status))
        };

        /// @abi table threshold
        struct threshold {
            account_name publisher;
            uint64_t total_voters;
            uint64_t quorum_threshold;
            uint32_t expiration_length;

            uint64_t primary_key() const { return publisher; }
            EOSLIB_SERIALIZE(threshold, (publisher)(quorum_threshold)(expiration_length))
        };

    typedef multi_index<N(documents), document> documents_table;
    typedef multi_index<N(proposals), proposal> proposals_table;

    typedef singleton<N(threshold), threshold> threshold_singleton;
    threshold_singleton thresh_singleton;
    threshold thresh_struct;
};