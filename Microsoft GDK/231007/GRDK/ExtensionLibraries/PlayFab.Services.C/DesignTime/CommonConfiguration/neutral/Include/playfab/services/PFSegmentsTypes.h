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
/// ChurnRiskLevel enum.
/// </summary>
enum class PFSegmentsChurnRiskLevel : uint32_t
{
    NoData,
    LowRisk,
    MediumRisk,
    HighRisk
};

/// <summary>
/// PFSegmentsGetSegmentResult data model.
/// </summary>
typedef struct PFSegmentsGetSegmentResult
{
    /// <summary>
    /// (Optional) Identifier of the segments AB Test, if it is attached to one.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* aBTestParent;

    /// <summary>
    /// Unique identifier for this segment.
    /// </summary>
    _Null_terminated_ const char* id;

    /// <summary>
    /// (Optional) Segment name.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* name;

} PFSegmentsGetSegmentResult;

/// <summary>
/// PFSegmentsGetPlayerSegmentsResult data model.
/// </summary>
typedef struct PFSegmentsGetPlayerSegmentsResult
{
    /// <summary>
    /// (Optional) Array of segments the requested player currently belongs to.
    /// </summary>
    _Maybenull_ _Field_size_(segmentsCount) PFSegmentsGetSegmentResult const* const* segments;

    /// <summary>
    /// Count of segments
    /// </summary>
    uint32_t segmentsCount;

} PFSegmentsGetPlayerSegmentsResult;

/// <summary>
/// PFSegmentsGetPlayerTagsRequest data model. This API will return a list of canonical tags which includes
/// both namespace and tag's name. If namespace is not provided, the result is a list of all canonical
/// tags. TagName can be used for segmentation and Namespace is limited to 128 characters.
/// </summary>
typedef struct PFSegmentsGetPlayerTagsRequest
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
    /// (Optional) Optional namespace to filter results by.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* playfabNamespace;

    /// <summary>
    /// Unique PlayFab assigned ID of the user on whom the operation will be performed.
    /// </summary>
    _Null_terminated_ const char* playFabId;

} PFSegmentsGetPlayerTagsRequest;

/// <summary>
/// PFSegmentsGetPlayerTagsResult data model.
/// </summary>
typedef struct PFSegmentsGetPlayerTagsResult
{
    /// <summary>
    /// Unique PlayFab assigned ID of the user on whom the operation will be performed.
    /// </summary>
    _Null_terminated_ const char* playFabId;

    /// <summary>
    /// Canonical tags (including namespace and tag's name) for the requested user.
    /// </summary>
    _Field_size_(tagsCount) const char* const* tags;

    /// <summary>
    /// Count of tags
    /// </summary>
    uint32_t tagsCount;

} PFSegmentsGetPlayerTagsResult;

/// <summary>
/// PFSegmentsAdCampaignAttribution data model.
/// </summary>
typedef struct PFSegmentsAdCampaignAttribution
{
    /// <summary>
    /// UTC time stamp of attribution.
    /// </summary>
    time_t attributedAt;

    /// <summary>
    /// (Optional) Attribution campaign identifier.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* campaignId;

    /// <summary>
    /// (Optional) Attribution network name.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* platform;

} PFSegmentsAdCampaignAttribution;

/// <summary>
/// PFSegmentsContactEmailInfo data model.
/// </summary>
typedef struct PFSegmentsContactEmailInfo
{
    /// <summary>
    /// (Optional) The email address.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* emailAddress;

    /// <summary>
    /// (Optional) The name of the email info data.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* name;

    /// <summary>
    /// (Optional) The verification status of the email.
    /// </summary>
    _Maybenull_ PFEmailVerificationStatus const* verificationStatus;

} PFSegmentsContactEmailInfo;

/// <summary>
/// PFSegmentsPlayerLinkedAccount data model.
/// </summary>
typedef struct PFSegmentsPlayerLinkedAccount
{
    /// <summary>
    /// (Optional) Linked account's email.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* email;

    /// <summary>
    /// (Optional) Authentication platform.
    /// </summary>
    _Maybenull_ PFLoginIdentityProvider const* platform;

    /// <summary>
    /// (Optional) Platform user identifier.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* platformUserId;

    /// <summary>
    /// (Optional) Linked account's username.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* username;

} PFSegmentsPlayerLinkedAccount;

/// <summary>
/// PFSegmentsPlayerLocation data model.
/// </summary>
typedef struct PFSegmentsPlayerLocation
{
    /// <summary>
    /// (Optional) City of the player's geographic location.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* city;

    /// <summary>
    /// The two-character continent code for this location.
    /// </summary>
    PFContinentCode continentCode;

    /// <summary>
    /// The two-character ISO 3166-1 country code for the country associated with the location.
    /// </summary>
    PFCountryCode countryCode;

    /// <summary>
    /// (Optional) Latitude coordinate of the player's geographic location.
    /// </summary>
    _Maybenull_ double const* latitude;

    /// <summary>
    /// (Optional) Longitude coordinate of the player's geographic location.
    /// </summary>
    _Maybenull_ double const* longitude;

} PFSegmentsPlayerLocation;

/// <summary>
/// PFSegmentsPlayerStatistic data model.
/// </summary>
typedef struct PFSegmentsPlayerStatistic
{
    /// <summary>
    /// (Optional) Statistic ID.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* id;

    /// <summary>
    /// (Optional) Statistic name.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* name;

    /// <summary>
    /// Current statistic value.
    /// </summary>
    int32_t statisticValue;

    /// <summary>
    /// Statistic version (0 if not a versioned statistic).
    /// </summary>
    int32_t statisticVersion;

} PFSegmentsPlayerStatistic;

/// <summary>
/// PFSegmentsPushNotificationRegistration data model.
/// </summary>
typedef struct PFSegmentsPushNotificationRegistration
{
    /// <summary>
    /// (Optional) Notification configured endpoint.
    /// </summary>
    _Maybenull_ _Null_terminated_ const char* notificationEndpointARN;

    /// <summary>
    /// (Optional) Push notification platform.
    /// </summary>
    _Maybenull_ PFPushNotificationPlatform const* platform;

} PFSegmentsPushNotificationRegistration;

/// <summary>
/// Dictionary entry for an associative array with PFSegmentsPlayerLocation values.
/// </summary>
typedef struct PFSegmentsPlayerLocationDictionaryEntry
{
    _Null_terminated_ const char* key;
    PFSegmentsPlayerLocation const* value;
} PFSegmentsPlayerLocationDictionaryEntry;

#pragma pop_macro("IN")

}
