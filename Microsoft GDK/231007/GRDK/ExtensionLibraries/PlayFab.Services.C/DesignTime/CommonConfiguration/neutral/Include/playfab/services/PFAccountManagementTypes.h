// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#if !defined(__cplusplus)
#error C++11 required
#endif

#pragma once

#include <playfab/core/PFPal.h>
#include <playfab/core/PFTypes.h>
#include <playfab/services/PFTypes.h>

extern "C"
{

#pragma push_macro("IN")
#undef IN

/// <summary>
/// PFAccountManagementGetAccountInfoRequest data model.
/// </summary>
typedef struct PFAccountManagementGetAccountInfoRequest
{
    /// <summary>
    /// (Optional) User email address for the account to find (if no Username is specified).
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* email;

    /// <summary>
    /// (Optional) Unique PlayFab identifier of the user whose info is being requested. Optional, defaults
    /// to the authenticated user if no other lookup identifier set.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* playFabId;

    /// <summary>
    /// (Optional) Title-specific username for the account to find (if no Email is set). Note that if
    /// the non-unique Title Display Names option is enabled for the title, attempts to look up users
    /// by Title Display Name will always return AccountNotFound.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* titleDisplayName;

    /// <summary>
    /// (Optional) PlayFab Username for the account to find (if no PlayFabId is specified).
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* username;

} PFAccountManagementGetAccountInfoRequest;

/// <summary>
/// PFAccountManagementGetAccountInfoResult data model. This API retrieves details regarding the player
/// in the PlayFab service. Note that when this call is used to retrieve data about another player (not
/// the one signed into the local client), some data, such as Personally Identifying Information (PII),
/// will be omitted for privacy reasons or to comply with the requirements of the platform belongs to.
/// The user account returned will be based on the identifier provided in priority order: PlayFabId, Username,
/// Email, then TitleDisplayName. If no identifier is specified, the currently signed in user's information
/// will be returned.
/// </summary>
typedef struct PFAccountManagementGetAccountInfoResult
{
    /// <summary>
    /// (Optional) Account information for the local user.
    /// </summary>
    _Maybenull_ PFUserAccountInfo const* accountInfo;

} PFAccountManagementGetAccountInfoResult;

/// <summary>
/// PFAccountManagementGetPlayerCombinedInfoRequest data model.
/// </summary>
typedef struct PFAccountManagementGetPlayerCombinedInfoRequest
{
    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

    /// <summary>
    /// Flags for which pieces of info to return for the user.
    /// </summary>
    PFGetPlayerCombinedInfoRequestParams const* infoRequestParameters;

    /// <summary>
    /// PlayFabId of the user whose data will be returned. If not filled included, we return the data
    /// for the calling player. .
    /// </summary>
    _Null_terminated_ const char* playFabId;

} PFAccountManagementGetPlayerCombinedInfoRequest;

/// <summary>
/// PFAccountManagementGetPlayerCombinedInfoResult data model. Returns whatever info is requested in
/// the response for the user. If no user is explicitly requested this defaults to the authenticated user.
/// If the user is the same as the requester, PII (like email address, facebook id) is returned if available.
/// Otherwise, only public information is returned. All parameters default to false.
/// </summary>
typedef struct PFAccountManagementGetPlayerCombinedInfoResult
{
    /// <summary>
    /// (Optional) Results for requested info.
    /// </summary>
    _Maybenull_ PFGetPlayerCombinedInfoResultPayload const* infoResultPayload;

    /// <summary>
    /// (Optional) Unique PlayFab assigned ID of the user on whom the operation will be performed.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* playFabId;

} PFAccountManagementGetPlayerCombinedInfoResult;

/// <summary>
/// PFAccountManagementGetPlayerProfileRequest data model. This API allows for access to details regarding
/// a user in the PlayFab service, usually for purposes of customer support. Note that data returned may
/// be Personally Identifying Information (PII), such as email address, and so care should be taken in
/// how this data is stored and managed. Since this call will always return the relevant information for
/// users who have accessed the title, the recommendation is to not store this data locally.
/// </summary>
typedef struct PFAccountManagementGetPlayerProfileRequest
{
    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

    /// <summary>
    /// Unique PlayFab assigned ID of the user on whom the operation will be performed.
    /// </summary>
    _Null_terminated_ const char* playFabId;

    /// <summary>
    /// (Optional) If non-null, this determines which properties of the resulting player profiles to
    /// return. For API calls from the client, only the allowed client profile properties for the title
    /// may be requested. These allowed properties are configured in the Game Manager "Client Profile
    /// Options" tab in the "Settings" section.
    /// </summary>
    _Maybenull_ PFPlayerProfileViewConstraints const* profileConstraints;

} PFAccountManagementGetPlayerProfileRequest;

/// <summary>
/// PFAccountManagementGetPlayerProfileResult data model.
/// </summary>
typedef struct PFAccountManagementGetPlayerProfileResult
{
    /// <summary>
    /// (Optional) The profile of the player. This profile is not guaranteed to be up-to-date. For a
    /// new player, this profile will not exist.
    /// </summary>
    _Maybenull_ PFPlayerProfileModel const* playerProfile;

} PFAccountManagementGetPlayerProfileResult;

/// <summary>
/// PFAccountManagementGetPlayFabIDsFromXboxLiveIDsRequest data model.
/// </summary>
typedef struct PFAccountManagementGetPlayFabIDsFromXboxLiveIDsRequest
{
    /// <summary>
    /// (Optional) The ID of Xbox Live sandbox.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* sandbox;

    /// <summary>
    /// Array of unique Xbox Live account identifiers for which the title needs to get PlayFab identifiers.
    /// The array cannot exceed 2,000 in length.
    /// </summary>
    _Field_size_(xboxLiveAccountIDsCount) const char* const* xboxLiveAccountIDs;

    /// <summary>
    /// Count of xboxLiveAccountIDs
    /// </summary>
    uint32_t xboxLiveAccountIDsCount;

} PFAccountManagementGetPlayFabIDsFromXboxLiveIDsRequest;

/// <summary>
/// PFAccountManagementXboxLiveAccountPlayFabIdPair data model.
/// </summary>
typedef struct PFAccountManagementXboxLiveAccountPlayFabIdPair
{
    /// <summary>
    /// (Optional) Unique PlayFab identifier for a user, or null if no PlayFab account is linked to the
    /// Xbox Live identifier.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* playFabId;

    /// <summary>
    /// (Optional) Unique Xbox Live identifier for a user.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* xboxLiveAccountId;

} PFAccountManagementXboxLiveAccountPlayFabIdPair;

/// <summary>
/// PFAccountManagementGetPlayFabIDsFromXboxLiveIDsResult data model. For XboxLive identifiers which
/// have not been linked to PlayFab accounts, null will be returned.
/// </summary>
typedef struct PFAccountManagementGetPlayFabIDsFromXboxLiveIDsResult
{
    /// <summary>
    /// (Optional) Mapping of Xbox Live identifiers to PlayFab identifiers.
    /// </summary>
    _Maybenull_ _Field_size_(dataCount) PFAccountManagementXboxLiveAccountPlayFabIdPair const* const* data;

    /// <summary>
    /// Count of data
    /// </summary>
    uint32_t dataCount;

} PFAccountManagementGetPlayFabIDsFromXboxLiveIDsResult;

/// <summary>
/// PFAccountManagementLinkCustomIDRequest data model.
/// </summary>
typedef struct PFAccountManagementLinkCustomIDRequest
{
    /// <summary>
    /// Custom unique identifier for the user, generated by the title.
    /// </summary>
    _Null_terminated_ const char* customId;

    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

    /// <summary>
    /// (Optional) If another user is already linked to the custom ID, unlink the other user and re-link.
    /// </summary>
    _Maybenull_ bool const* forceLink;

} PFAccountManagementLinkCustomIDRequest;

/// <summary>
/// PFAccountManagementLinkOpenIdConnectRequest data model.
/// </summary>
typedef struct PFAccountManagementLinkOpenIdConnectRequest
{
    /// <summary>
    /// A name that identifies which configured OpenID Connect provider relationship to use. Maximum
    /// 100 characters.
    /// </summary>
    _Null_terminated_ const char* connectionId;

    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

    /// <summary>
    /// (Optional) If another user is already linked to a specific OpenId Connect user, unlink the other
    /// user and re-link.
    /// </summary>
    _Maybenull_ bool const* forceLink;

    /// <summary>
    /// The JSON Web token (JWT) returned by the identity provider after login. Represented as the id_token
    /// field in the identity provider's response. Used to validate the request and find the user ID (OpenID
    /// Connect subject) to link with.
    /// </summary>
    _Null_terminated_ const char* idToken;

} PFAccountManagementLinkOpenIdConnectRequest;

/// <summary>
/// PFAccountManagementClientLinkXboxAccountRequest data model.
/// </summary>
typedef struct PFAccountManagementClientLinkXboxAccountRequest
{
    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

    /// <summary>
    /// (Optional) If another user is already linked to the account, unlink the other user and re-link.
    /// </summary>
    _Maybenull_ bool const* forceLink;

#if HC_PLATFORM == HC_PLATFORM_GDK
    /// <summary>
    /// XUser of the account to link to.
    /// </summary>
    XUserHandle user;
#else
    /// <summary>
    /// Token provided by the Xbox Live SDK/XDK method GetTokenAndSignatureAsync("POST", "https://playfabapi.com/",
    /// "").
    /// </summary>
    _Null_terminated_ const char* xboxToken;
#endif

} PFAccountManagementClientLinkXboxAccountRequest;

/// <summary>
/// PFAccountManagementReportPlayerClientRequest data model.
/// </summary>
typedef struct PFAccountManagementReportPlayerClientRequest
{
    /// <summary>
    /// (Optional) Optional additional comment by reporting player.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* comment;

    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

    /// <summary>
    /// Unique PlayFab identifier of the reported player.
    /// </summary>
    _Null_terminated_ const char* reporteeId;

} PFAccountManagementReportPlayerClientRequest;

/// <summary>
/// PFAccountManagementReportPlayerClientResult data model. Players are currently limited to five reports
/// per day. Attempts by a single user account to submit reports beyond five will result in Updated being
/// returned as false.
/// </summary>
typedef struct PFAccountManagementReportPlayerClientResult
{
    /// <summary>
    /// The number of remaining reports which may be filed today.
    /// </summary>
    int32_t submissionsRemaining;

} PFAccountManagementReportPlayerClientResult;

/// <summary>
/// PFAccountManagementUnlinkCustomIDRequest data model.
/// </summary>
typedef struct PFAccountManagementUnlinkCustomIDRequest
{
    /// <summary>
    /// (Optional) Custom unique identifier for the user, generated by the title. If not specified, the
    /// most recently signed in Custom ID will be used.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* customId;

    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

} PFAccountManagementUnlinkCustomIDRequest;

/// <summary>
/// PFAccountManagementUnlinkOpenIdConnectRequest data model.
/// </summary>
typedef struct PFAccountManagementUnlinkOpenIdConnectRequest
{
    /// <summary>
    /// A name that identifies which configured OpenID Connect provider relationship to use. Maximum
    /// 100 characters.
    /// </summary>
    _Null_terminated_ const char* connectionId;

    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

} PFAccountManagementUnlinkOpenIdConnectRequest;

/// <summary>
/// PFAccountManagementClientUnlinkXboxAccountRequest data model.
/// </summary>
typedef struct PFAccountManagementClientUnlinkXboxAccountRequest
{
    /// <summary>
    /// (Optional) The optional custom tags associated with the request (e.g. build number, external
    /// trace identifiers, etc.).
    /// </summary>
    _Maybenull_ _Field_size_(customTagsCount) struct PFStringDictionaryEntry const* customTags;

    /// <summary>
    /// Count of customTags
    /// </summary>
    uint32_t customTagsCount;

} PFAccountManagementClientUnlinkXboxAccountRequest;

/// <summary>
/// PFAccountManagementClientUpdateAvatarUrlRequest data model.
/// </summary>
typedef struct PFAccountManagementClientUpdateAvatarUrlRequest
{
    /// <summary>
    /// URL of the avatar image. If empty, it removes the existing avatar URL.
    /// </summary>
    _Null_terminated_ const char* imageUrl;

} PFAccountManagementClientUpdateAvatarUrlRequest;

#pragma pop_macro("IN")

}
