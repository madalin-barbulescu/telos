#include <eosiolib/eosio.hpp>

#include "../trail.service/trail.connections/trailconn.voting.hpp" //Import trailservice voting data definitions
#include "../trail.service/trail.connections/trailconn.system.hpp"

class workerproposal : public contract {
    public:

      workerproposal(account_name self);

      ~workerproposal();

      /// @abi action
      void submission(account_name proposer, std::string title, std::string text, uint16_t cycles, std::string ipfs_location, asset amount, account_name send_to);

      /// @abi action
      void vote(uint64_t proposal_id, uint16_t direction, account_name voter);

      /// @abi action
      void claim(uint64_t proposal_id);

      //@abi table proposals i64
      struct proposal {
        uint64_t                id;
        std::string             title;
        std::string             ipfs_location;
        std::string             text_hash;
        uint16_t                cycles;
        uint64_t                amount;
        account_name            send_to;
        uint64_t                fee;
        account_name            proposer;
        uint64_t                yes_count;
        uint64_t                no_count;
        uint64_t                abstain_count;
        uint16_t                last_cycle_check;
        bool                    last_cycle_status;
        uint32_t                created;
        uint64_t                outstanding;

        auto primary_key() const { return id; }
      };

      struct wp_env {
        account_name publisher;
        uint64_t total_voters;
        uint64_t quorum_threshold;
        uint32_t cycle_duration;
        uint16_t fee_percentage;
        uint64_t fee_min;

        uint64_t primary_key() const { return publisher; }
      };
      wp_env wp_env_struct;

      typedef eosio::multi_index< N(proposals), proposal> proposals;
      typedef singleton<N(wp_env), wp_env> wp_environment_singleton;

      wp_environment_singleton wp_env_singleton;    

    protected:

      void update_env();
      bool check_fee_threshold(proposal p);
      bool check_threshold(proposal p);
      void update_outstanding(proposal &p);
};
