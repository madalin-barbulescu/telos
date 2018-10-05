#pragma once

#include <trail.connections/trailconn.voting.hpp> //Import trailservice voting data definitions

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>
#include <string>

using namespace eosio;

class trail : public contract {
    public:

        /**
         * Constructor sets environment singleton upon contract deployment, and gets environment for every action call.
        */
        trail(account_name self);

        /**
         * Destructor saves final state of environment struct at the end of every action call.
        */
        ~trail();

        /**
         * Registers a new token registry and its native asset.
         * @param native - native asset of token registry
         * @param publisher - account publishing token registry
        */
        void regtoken(asset native, account_name publisher);

        /**
         * Unregisters an existing token registry.
         * @param native - native asset of the token registry
         * @param publisher - account where the token registry is published
        */
        void unregtoken(asset native, account_name publisher);

        /**
         * Registers an account with a new VoterID.
         * @param voter - account for which to create a new VoterID
        */
        void regvoter(account_name voter);

        /**
         * Unregisters an account's existing VoterID.
         * @param voter - account owning VoterID to be unregistered 
        */
        void unregvoter(account_name voter);

        /**
         * Adds a receipt to the receipt list on a VoterID.
         * @param vote_code - 
         * @param vote_scope - 
         * @param vote_key - 
         * @param direction - 
         * @param weight - 
         * @param voter - 
        */
        void addreceipt(uint64_t vote_code, uint64_t vote_scope, uint64_t vote_key, uint16_t direction, account_name voter);

        /**
         * Removes a receipt from the receipt list on a VoterID.
         * @param vote_code - 
         * @param vote_scope - 
         * @param vote_key - 
         * @param voter - 
        */
        void rmvreceipt(uint64_t vote_code, uint64_t vote_scope, uint64_t vote_key, account_name voter);

    protected:

        /// @abi table registries i64
        struct registration {
            asset native;
            account_name publisher;

            uint64_t primary_key() const { return native.symbol.name(); }
            uint64_t by_publisher() const { return publisher; }
        };

        typedef multi_index<N(registries), registration> registries_table;

        environment_singleton env_singleton;
        environment env_struct;
};