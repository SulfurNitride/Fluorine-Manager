#ifndef MODORGANIZER_NEXUSCREDENTIALSTATE_INCLUDED
#define MODORGANIZER_NEXUSCREDENTIALSTATE_INCLUDED

#include "apiuseraccount.h"
#include "nexusoauthtokens.h"

#include <cstdint>
#include <optional>

// Credential edits in the Settings dialog are applied immediately.  These
// snapshots preserve both the credential store and the independently cached
// runtime state so Cancel and a refused restart can be transactional.
struct NexusCredentialStoreSnapshot
{
  std::optional<QString> apiKey;
  std::optional<NexusOAuthTokens> oauthTokens;

  bool operator==(const NexusCredentialStoreSnapshot&) const = default;
};

enum class NexusValidationState
{
  NotChecked,
  Valid,
  Invalid
};

// A Settings transaction must preserve work that was already in flight before
// the dialog opened. The exact OAuth UI states collapse into the two
// restartable operations: an authorization grant or a token refresh.
enum class NexusOAuthOperation
{
  None,
  Authorization,
  Refresh
};

struct NexusLiveCredentialSnapshot
{
  std::optional<NexusOAuthTokens> tokens;
  NexusValidationState validationState{NexusValidationState::NotChecked};
  bool validationWaiting{false};
  NexusOAuthOperation oauthOperation{NexusOAuthOperation::None};
  std::uint64_t oauthFlowGeneration{0};
  std::uint64_t oauthAttemptGeneration{0};
  APIUserAccount account;

  bool operator==(const NexusLiveCredentialSnapshot&) const = default;
};

#endif  // MODORGANIZER_NEXUSCREDENTIALSTATE_INCLUDED
