
#include "multi/multi_core/multi_core_platform.h"
#include "multi/multi_core/multi_core_platform_internal.h"
#include "multi_game_friend.h"

#if defined(__NX__)
	#include <nn/err.h>
	#include <nn/friends.h>
	#include <nex.h>
#endif

#if defined(__XBOX3)
#include "vlib/durango_clr/vlive_clr.hpp"
#endif

#if defined(__PS4)	// HVS_JRS 3/6/2014 and USE_TOOLKIT
#include <np_toolkit.h>	// HVS_JRS 3/5/2014 add NpToolkit
#include <np_toolkit/user_defines.h>
#endif

#if defined(__PC_GP)
#include "multi/multi_core/multi_core_platform_pcgp.h"
#include <xsapi-c/services_c.h>
#include <xsapi-c/xbox_live_context_c.h>
#include <xsapi-c/types_c.h>
#include <xsapi-c/profile_c.h>
#include <xsapi-c/presence_c.h>
#include <xsapi-c/multiplayer_c.h>
#include <xsapi-c/multiplayer_manager_c.h>
#endif

#if defined(__LIVE_ENABLED) || defined(__PS3) || defined(__STEAM) || defined(__PS4) || defined(__XBOX3) || defined(__NX__) || defined(__PC_GP) // HVS_MMK[PRINCE] 12/10/2013 : TODO add networking

// --------------------
//
// Defines
//
// --------------------
#if defined(__LIVE_ENABLED)
	#define MAX_QUERIES_AT_ONCE		(50)
	#define QUERY_BUFFER_SIZE		(MAX_QUERIES_AT_ONCE * sizeof(XONLINE_FRIEND))
#endif

// --------------------
//
// Helper Macros
//
// --------------------

// --------------------
//
// Enumerated Types
//
// --------------------
#if defined(__LIVE_ENABLED)  || defined(__XBOX3)
	enum query_operation_type 
	{
		QUERY_OPERATION_NONE = 0,
		QUERY_OPERATION_FRIENDS,
		QUERY_OPERATION_JOIN,
	};
#endif

#if defined(__PC_GP)
	enum live_query_op_state {
		LIVE_QUERY_OP_IDLE,
		LIVE_QUERY_OP_ACTIVE,
		LIVE_QUERY_OP_COMPLETED,
		LIVE_QUERY_OP_RESULT_PROCESSED,
	};
#endif

// --------------------
//
// Structures
//
// --------------------
struct multi_game_friend_invite
{
	multi_game_friend *friend_ptr;
	const TCHAR *message;

	multi_game_friend_invite *prev;
};

// Keep track of who we sent invites to and when we sent them.
struct multi_game_friend_invite_sent
{
	multi_player_uid	uid;
	timestamp_realtime	sent_ts;
};

// --------------------
//
// Classes
//
// --------------------

void multi_game_friend::invalidate()
{
	player_name[0] = 0;
	player_uid.invalidate();
	status_flags = 0;

#if defined(__LIVE_ENABLED)
	memset(&session_id, 0, sizeof(session_id));
#endif // __LIVE_ENABLED
#if defined(__XBOX3)
	memset(&session_id, 0, sizeof(session_id));
#endif
#if defined(__NX__)
	gathering_id = INVALID_GATHERINGID;
#endif // __LIVE_ENABLED
#if defined(__PC_GP)
	memset(&handle_id, 0, sizeof(handle_id));
#endif
}

// --------------------
//
// Global Variables
//
// --------------------

#if defined(__NX__)
	extern nn::nex::MatchmakeExtensionClient* Matchmaking_client;
#endif

#if defined(__PC_GP)
	extern XblContextHandle Live_context;
	extern const XblMultiplayerEvent* Live_multiplayer_events;
	extern size_t Live_multiplayer_events_count;
#endif

// --------------------
//
// Local Variables
//
// --------------------

// For the search system
static bool Query_in_progress = false;
static multi_game_friend_get_list_cb Get_list_cb = NULL;
static multi_game_friend_get_uid_list_cb Get_uid_list_cb = NULL;
static multi_game_friend_check_cb Check_friend_cb = NULL;
static multi_game_friend *Friend_list_out = NULL;
static multi_player_uid *Uid_list_out = NULL;
static bool Are_friends[MAX_CONNECTIONS];
static uint Friend_list_filter = MULTI_FRIEND_FLAG_NONE;
static int Result_count = 0;				// Result count
static int Result_count_total = 0;		// This returns result count, including before Start_index
static int Max_count = 0;
static int Current_offset = 0;			// The current offset into the array
static int Start_index = 0;
static void* Search_user_data = NULL;

// For the invite system
static bool Inviting = false;
static multi_game_friend_invite Pending_invites[MULTI_GAME_FRIEND_MAX_PENDING_INVITES]; 
static multi_game_friend_invite *Current_invites = NULL;	// Points to one of the Pending invites
static multi_game_friend_invite *Free_invites = NULL;

#if defined(__LIVE_ENABLED)
	query_operation_type Query_operation_type = QUERY_OPERATION_NONE;

	static XOVERLAPPED Query_operation;
	static HANDLE Query_handle = INVALID_HANDLE_VALUE;
	static ubyte Query_buffer_pool[QUERY_BUFFER_SIZE];
	static DWORD *Query_buffer = (DWORD*)&Query_buffer_pool[0];

	static XNKID Session_id;

	static XOVERLAPPED Invite_operation;

#elif defined(__XBOX3)
	query_operation_type Query_operation_type = QUERY_OPERATION_NONE;

#elif defined(__PS3) 
	static uint Current_operation = 0;				// Keep track of the current operation in question
	static bool Operation_in_progress = false;	// An operation is currently in progress
	static bool Operation_has_finished = false;	//	A thread is currently active

#elif defined(__NX__)
	static nn::friends::Friend Friend_list[nn::friends::FriendCountMax];
	static int Friend_list_size = 0;
	static nn::nex::qList<nn::nex::FindMatchmakeSessionByParticipantResult> Matchmake_result;
	static nn::nex::ProtocolCallContext Matchmake_ctx;

#elif defined(__PC_GP)
	static live_query_op_state live_friend_search_profile_state = LIVE_QUERY_OP_IDLE;
	static live_query_op_state live_friend_search_presence_state = LIVE_QUERY_OP_IDLE;
	static live_query_op_state live_friend_serach_activities_state = LIVE_QUERY_OP_IDLE;

	std::vector<XblUserProfile> live_friends_profiles;
	std::vector<XblPresenceRecordHandle> live_friends_presence_records;
	std::vector<XblMultiplayerActivityDetails> live_friends_activities;
	std::vector<uint64_t> live_friends_user_ids;

	uint32_t titleIds[] = { live_get_title_id() };
	const XblPresenceDeviceType const deviceTypes[] = { XblPresenceDeviceType::WindowsOneCore };
	XblPresenceQueryFilters live_presence_query_filters = {
		&deviceTypes[0],
		1,
		&titleIds[0],
		1,
		XblPresenceDetailLevel::All,
		true,
		false
	};

#endif

const int INVITE_TIMER_MS = 15000;
const int MAX_INVITES_SENT = 10;
static farray<multi_game_friend_invite_sent, MAX_INVITES_SENT> Invites_sent;

// --------------------
//
// Console Functions
//
// --------------------
#if defined(_DEBUG)

	CONSOLEF( multi_show_invite_queue_count, "Shows the current count of the queues." )
	{
		int free_spaces = 0;
		int used_spaces = 0;

		multi_game_friend_invite *invite = Current_invites;
		while (invite) {
			used_spaces++;
			invite = invite->prev;
		}

		invite = Free_invites;
		while (invite) {
			free_spaces++;
			invite = invite->prev;
		}

		console_printf( "Free: %i,   Used: %i\n", free_spaces, used_spaces );
	}

	// Used in the following console function
	#define MAX_TEST_FRIENDS (10)
	static multi_game_friend Friends_test[MAX_TEST_FRIENDS];
	static int Friend_test_count = 0;
	static void get_friend_list_callback( multi_game_friend *friends, int results, void *user_data ) 
	{
		V_ASSERT_RETURN( results <= MAX_TEST_FRIENDS );
		console_printf( "Friends returned: %i\n", results);	
		for (int i = 0; i < results; i++) {
			console_printf( "-- %i: %ls : %u\n", i, friends[i].player_name, friends[i].status_flags );
		}

		Friend_test_count = results;
	}

	CONSOLEF( multi_get_friend_list, "Gets your friend list.  This is ASYNC, so give it a little time." )
	{
		if (Dc_command) {

			int count = 10;
			int offset = 0;

			if (console_get_arg( CONSOLE_ARG_INT ), true ) {
				count = Console_arg_int;
			}

			if (console_get_arg( CONSOLE_ARG_INT ), true ) {
				offset = Console_arg_int;
			}

			count = V_MIN( count, MAX_TEST_FRIENDS );
			if (multi_game_friend_get_list( Friends_test, MULTI_FRIEND_FLAG_NONE, offset, count, get_friend_list_callback, NULL )) {
				console_printf( "Started Query...\n" );
			} else {
				console_printf( "Failed to start Query...\n" );
			}

		} else {
			console_printf( "multi_get_friend_list [<count> [<offset>]]\n" );
		}
	}

	CONSOLEF( multi_send_friend_invite, "Sends an invite to a friend returned by multi_get_friend_list." )
	{
		if (Dc_command) {
			int index = 0;
			if (console_get_arg( CONSOLE_ARG_INT, false )) {
				index = Console_arg_int;
				if ((index < Friend_test_count) 
					&& (multi_game_friend_send_invite( &(Friends_test[index]), TEXT("Testing, Testing!")) == MULTI_INVITE_SUCCESS) ) {
					
					console_printf( "Sent an invite to %ls.\n", Friends_test[index].player_name );
				}
			} else {
				console_printf( "Failed to send invite.\n" );
			}
		} else {
			console_printf( "multi_send_friend_invite <index>\n" );
		}
	}

	CONSOLEF( multi_join_friend, "Join a friend by offset returned by multi_get_friend_list" )
	{
		if (Dc_command) {
			int index = 0;
			if (console_get_arg( CONSOLE_ARG_INT, false )) {
				index = Console_arg_int;
				if ( (index < Friend_test_count) 
					&& multi_game_friend_join( &(Friends_test[index]) ) ) {

					console_printf( "Tried to join friend %ls.\n", Friends_test[index].player_name );
				}
			} else {
				console_printf( "Failed to join, friend is not marked as joinable.\n" );
			}
		} else {
			console_printf( "multi_join_friend <index>\n" );
		}
	}

#endif


// --------------------
//
// Internal Functions
//
// --------------------

#if defined(__LIVE_ENABLED)

	// Cleanup all LIVE sepcific search variables
	//
	static void cleanup_query_variables()
	{
		memset( &Query_operation, 0, sizeof(XOVERLAPPED) );
		if (Query_handle != INVALID_HANDLE_VALUE) {
			CloseHandle( Query_handle );
			Query_handle = INVALID_HANDLE_VALUE;
		}
	}

	// Start the LIVE search operation
	//
	static bool start_search_operation()
	{
		V_ASSERT( Query_in_progress );

		cleanup_query_variables();
		
		DWORD size = 0;
		DWORD result = XFriendsCreateEnumerator( multi_core_platform_get_active_controller(),
			Current_offset, 
			MAX_QUERIES_AT_ONCE,
			&size, 
			&Query_handle );

		V_ASSERT( size <= QUERY_BUFFER_SIZE );

		if (result != ERROR_SUCCESS) {
			return false;
		} else {
			result = XEnumerate( Query_handle,
				Query_buffer,
				QUERY_BUFFER_SIZE,
				NULL, 
				&Query_operation );

			return (result == ERROR_IO_PENDING);
		}
	}

	// Start the join operation - which isn't really starting the join so much
	// as it's doing a search for the session with matching ID so I can
	// get the info to let the normal join system handle it
	//
	static bool start_join_operation( XNKID &session_id )
	{
		V_ASSERT( Query_in_progress );

		memcpy( &Session_id, &session_id, sizeof(XNKID) );

		DWORD size = 0;
		DWORD result = XSessionSearchByID( Session_id, 
			multi_core_platform_get_active_controller(), 
			&size, NULL, 
			NULL );

		if (result != ERROR_INSUFFICIENT_BUFFER) {
			return false;
		}

		V_ASSERT( size <= QUERY_BUFFER_SIZE );
		result = XSessionSearchByID( Session_id, 
			multi_core_platform_get_active_controller(), 
			&size, (PXSESSION_SEARCHRESULT_HEADER)Query_buffer,
			&Query_operation );

		return (result == ERROR_IO_PENDING);
	}

	// Start the search
	//
	static bool start_search()
	{
		Result_count = 0;
		
		Query_in_progress = true;
		Query_operation_type = QUERY_OPERATION_FRIENDS;
		bool started = start_search_operation();
		if (!started) {
			cleanup_query_variables();
			Query_in_progress = false;
		}

		return started;
	}

	// Start the join process
	// This is mutually exclusive with getting a friends list
	//
	static bool start_join( multi_game_friend *pfriend ) 
	{
		Query_in_progress = true;
		Query_operation_type = QUERY_OPERATION_JOIN;
		bool started = start_join_operation( pfriend->session_id );
		if (!started) {
			cleanup_query_variables();
			Query_in_progress = false;
		}

		return started;
	}


	// Register the friends
	//
	static void register_friends( int count )
	{
		XONLINE_FRIEND *friends = reinterpret_cast<XONLINE_FRIEND*>(Query_buffer);

		for (int i = 0; i < count; i++) {
			
			uint friend_state = 0;

			bool request_pending = (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST) || (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST);
			if (request_pending) {
				continue;
			}

			if ( (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_ONLINE) ) {

				friend_state |= MULTI_FRIEND_FLAG_ONLINE;
			}

			if (friend_state & MULTI_FRIEND_FLAG_ONLINE) {

				// If they're not on-line, I don't care about them.
				if (friends[i].dwTitleID == TITLEID_THIS_GAME) {
					friend_state |= MULTI_FRIEND_FLAG_IN_GAME;
				}

				if ((friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_JOINABLE) || (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_JOINABLE_FRIENDS_ONLY)) {
					// Accept joinable for friends only since... well, I'm getting my friends list aren't I?
					friend_state |= MULTI_FRIEND_FLAG_JOINABLE;
				}
			}

			// Check if they meet ALL of our filters
			if ((friend_state & Friend_list_filter) == Friend_list_filter) {

				if (Result_count_total >= Start_index) {
					if (Get_list_cb != NULL) {
						strncpy_safe(Friend_list_out[Result_count].player_name, friends[i].szGamertag, PLAYER_NAME_LENGTH);
						Friend_list_out[Result_count].player_uid.set( friends[i].xuid );
						Friend_list_out[Result_count].status_flags = friend_state;
						Friend_list_out[Result_count].session_id = friends[i].sessionID;

						Friend_list_out[Result_count].presence[0] = 0;
						if (friends[i].cchRichPresence > 0) {
							strncpy_safe(Friend_list_out[Result_count].presence, friends[i].wszRichPresence, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
						} 

					} else if (Get_uid_list_cb) {
						Uid_list_out[Result_count].set( friends[i].xuid );
					} else if (Check_friend_cb) {
						int j = 0;
						for (j = 0; j < Max_count; j++) {
							if (Uid_list_out[j].is_equal(friends[i].xuid)) {
								break;
							}
						}

						if (j < Max_count) {
							Are_friends[j] = true;
						}
					}

					Result_count++;
				}

				Result_count_total++;
			}
		}
	}

	// continue the search, start the next batch of searchings
	static bool continue_search()
	{
		V_ASSERT_RETURN_VALUE( Query_in_progress, false );

		// I am DONE!
		Current_offset += MAX_QUERIES_AT_ONCE;
		if (Result_count >= Max_count) {
			return false;
		}

		return start_search_operation();
	}

	// Finish the search, cleanup and exit
	//
	static void finish_search()
	{
		V_ASSERT_RETURN( Query_in_progress );

		if (Get_list_cb) {
			Get_list_cb( Friend_list_out, Result_count, Search_user_data );
		} else if (Get_uid_list_cb) {
			Get_uid_list_cb( Uid_list_out, Result_count, Search_user_data );
		} else if (Check_friend_cb) {
			Check_friend_cb( Uid_list_out, Are_friends, Search_user_data );
		}

		cleanup_query_variables();
		Query_in_progress = false;
		Query_operation_type = QUERY_OPERATION_NONE;
	}

	// Join has finsihed
	static void finish_join() 
	{
		V_ASSERT_RETURN( Query_in_progress );
		cleanup_query_variables();
		Query_in_progress = false;
		Query_operation_type = QUERY_OPERATION_NONE;
	}

#elif defined(__PS3)

	// Get the search results
	static bool start_search()
	{
		Result_count = 0;
		// Current_ps3_operation++;

		// Should probably do this on a thread if it proves to be too slow
		if (multi_core_platform_is_connected_to_internet()) {
			uint32_t count = 0;
			// uint current_operation = Current_ps3_operation;

			int ret = sceNpBasicGetFriendListEntryCount( &count );
			if (ret < 0) {
				return false;
			}

			// Searching past the end
			if ((int)count <= Current_offset) {
				Result_count = 0;
			} else {
				Result_count_total = 0;
				SceNpUserInfo user_info;
				SceNpBasicPresenceDetails2 details;

				details.struct_size = sizeof(SceNpBasicPresenceDetails2);

				uint32_t i = 0;
				for (i = 0; i < count; i++) {
					ret = sceNpBasicGetFriendPresenceByIndex2( i, &user_info, &details, 0 );	
				
					if (ret >= 0) {

						uint friend_state = 0;

						if (details.state != SCE_NP_BASIC_PRESENCE_STATE_OFFLINE) {
							friend_state |= MULTI_FRIEND_FLAG_ONLINE;
						}

						if (details.state == SCE_NP_BASIC_PRESENCE_STATE_IN_CONTEXT) {
							friend_state |= MULTI_FRIEND_FLAG_IN_GAME;
						}

						// First byte of the presence data contains flags we'll need to check.
						if (details.data[0] & PS3_PRESENCE_SESSION_FLAG_JOINABLE) {	
							friend_state |= MULTI_FRIEND_FLAG_JOINABLE;
						}

						if ((friend_state & Friend_list_filter) == Friend_list_filter) {

							if (Result_count_total >= Start_index) {
								if (Get_list_cb != NULL) {

									// PJA:
									// The online ID can be between 3 and 16 bytes long. The SceNpOnlineId.data field
									// is 16 bytes long. If the online ID is less than 16 bytes long, then the null
									// terminator is inluded in onlineId.data. If it is exactly 16 bytes long, the
									// null terminator is in SceNpOnlineId.term, immediately following.
									strncpy_safe( Friend_list_out[Result_count].player_name, user_info.userId.handle.data, PLAYER_NAME_LENGTH );
									Friend_list_out[Result_count].player_uid.set( user_info.userId );
									Friend_list_out[Result_count].status_flags = friend_state;

									Friend_list_out[Result_count].presence[0] = 0;
									//strncpy_safe(Friend_list_out[Result_count].presence, details.status, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
									mbstowcs(Friend_list_out[Result_count].presence, details.status, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
									Result_count++;
								} else if (Get_uid_list_cb != NULL) {
									Uid_list_out[Result_count].set( user_info.userId );
									Result_count++;
								} else if (Check_friend_cb != NULL) {
									int j = 0;
									for (j = 0; j < Max_count; j++) {
										if (Uid_list_out[j].is_equal( user_info.userId )) {
											// We only increase the Result_count if I found my friend
											Are_friends[j] = true;
											Result_count++;
											break;
										}
									}
								} else {
									Result_count++;
								}
							}
							
							Result_count_total++;
						}

						if (Result_count >= Max_count) {
							// And we're done
							break;
						}
					}
				}
			}
		
			if (Get_uid_list_cb) {
				Get_uid_list_cb( Uid_list_out, Result_count, Search_user_data );
			} else if (Get_list_cb) {
				Get_list_cb( Friend_list_out, Result_count, Search_user_data );
			} else if (Check_friend_cb) {
				Check_friend_cb( Uid_list_out, Are_friends, Search_user_data );	
			}

			return true;

		} else {
			return false;
		}
	}

	// Start a join, this is pretty much the same as search, except I'm going to loop through all my friends, and join the player
	// that matches pfriend, if I don't find one, I'm going to fail the join
	//
	static bool start_join( multi_game_friend *pfriend )
	{
		// Should probably do this on a thread if it proves to be too slow
		if (multi_core_platform_is_connected_to_internet()) {
			uint32_t count = 0;
			// uint current_operation = Current_ps3_operation;

			int ret = sceNpBasicGetFriendListEntryCount( &count );
			
			// Searching past the end
			if (ret >= 0) {

				Result_count_total = 0;
				SceNpUserInfo user_info;
				SceNpBasicPresenceDetails details;

				uint32_t i = 0;
				for (i = 0; i < count; i++) {
					ret = sceNpBasicGetFriendPresenceByIndex( i, &user_info, &details, 0 );	
				
					if (ret >= 0) {

						if (pfriend->player_uid.is_equal(user_info.userId)) {
							bool joinable = (details.data[0] != 0);
							if (joinable) {
								// Join them
								multi_core_ps3_set_join_info( &(details.data[1]), details.size - 1 );
								return true;
							} else {
								break;
							}
						}
					} // Got info
				} // Loop for friends
			} // Got friends
		} // Connected to internet

		multi_core_platform_clear_join_info(true);
		return false;
	}


#elif defined(__STEAM) && !defined(__PC_GP)

	// Get the search results
	static bool start_search()
	{
		Result_count = 0;
		Result_count_total = 0;
		
		// Can't do anything if we ain't connected.
		if(!multi_core_platform_is_connected_to_internet() || SteamFriends() == NULL) {
			return false;
		}

		// Get a list of all the friends and loop through checking requirements.
		int count = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
		int i;
		for(i = 0; i < count; i++) {
			CSteamID friend_id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);

			// Check the online status.
			if((Friend_list_filter & MULTI_FRIEND_FLAG_ONLINE) != 0) {
				if(SteamFriends()->GetFriendPersonaState(friend_id) == k_EPersonaStateOffline) {
					continue;
				}
			}

			// Check to see if they're in game.
			FriendGameInfo_t game_info;
			bool in_game = SteamFriends()->GetFriendGamePlayed(friend_id, &game_info);
			if((Friend_list_filter & MULTI_FRIEND_FLAG_IN_GAME) != 0 && (!in_game || game_info.m_gameID.AppID() != STEAM_APP_ID)) {
				continue;
			}

			// Check to make sure they're joinable.
			if((Friend_list_filter & MULTI_FRIEND_FLAG_IN_GAME) != 0 && !game_info.m_steamIDLobby.IsValid())
			{
				continue;
			}

			// Only add starting from the start index.
			if (Result_count_total >= Start_index) {
				if (Get_list_cb != NULL) {
					wchar_t wide_name[PLAYER_NAME_LENGTH];
					copy_to_unicode(wide_name, SteamFriends()->GetFriendPersonaName(friend_id), PLAYER_NAME_LENGTH);
					localization_compress_string(Friend_list_out[Result_count].player_name, PLAYER_NAME_LENGTH, wide_name);
					Friend_list_out[Result_count].player_uid.set( friend_id );
					Friend_list_out[Result_count].status_flags = Friend_list_filter;
					Result_count++;
				} else if (Get_uid_list_cb != NULL) {
					Uid_list_out[Result_count].set( friend_id );
					Result_count++;
				} else if (Check_friend_cb != NULL) {
					int j = 0;
					for (j = 0; j < Max_count; j++) {
						if (Uid_list_out[j].is_equal( friend_id )) {
							// We only increase the Result_count if I found my friend
							Are_friends[j] = true;
							Result_count++;
							break;
						}
					}
				} else {
					Result_count++;
				}
			}
			Result_count_total++;

			if (Result_count >= Max_count) {
				// And we're done
				break;
			}
		}

		if (Get_uid_list_cb) {
			Get_uid_list_cb( Uid_list_out, Result_count, Search_user_data );
		} else if (Get_list_cb) {
			Get_list_cb( Friend_list_out, Result_count, Search_user_data );
		} else if (Check_friend_cb) {
			Check_friend_cb( Uid_list_out, Are_friends, Search_user_data );	
		}

		return true;
	}

	void multi_core_platform_internal_request_and_join_session(CSteamID const &session_id);
	// Start a join, this is pretty much the same as search, except I'm going to loop through all my friends, and join the player
	// that matches pfriend, if I don't find one, I'm going to fail the join
	//
	static bool start_join( multi_game_friend *pfriend )
	{
		// Can't do anything if we ain't connected.
		if(!multi_core_platform_is_connected_to_internet() || SteamMatchmaking() == NULL || SteamFriends() == NULL) {
			return false;
		}

		// See if the person we're trying to join is really in a game.
		CSteamID friend_id = pfriend->player_uid.get_steam_uid();
		FriendGameInfo_t game_info;
		bool in_game = SteamFriends()->GetFriendGamePlayed(friend_id, &game_info);
		if(!in_game || game_info.m_gameID.AppID() != STEAM_APP_ID || !game_info.m_steamIDLobby.IsValid()) {
			multi_core_platform_clear_join_info(true);
			return false;
		}

		CSteamID lobby_id = game_info.m_steamIDLobby;
		multi_core_platform_internal_request_and_join_session(lobby_id);

		return true;
	}

#elif defined(__PS4)	// HVS_JRS 3/6/2014 add friends list

extern SceUserServiceUserId GetNetworkPlayerID(void);
extern int nGetFriendCount(SceUserServiceUserId userId);
extern int nFindIndexOfUserId(SceUserServiceUserId userId);
extern const SceNpId *GetFriendSceNpId(int index, SceUserServiceUserId userId);
extern const sce::Toolkit::NP::PresenceInfo *GetFriendPresenceInfo(int index, SceUserServiceUserId userId);

// HVS_JRS 9/16/2014 locate title id and title secret in one place
//extern SceNpTitleId np_title_id;

// Get the search results
	static bool start_search()
	{
		Result_count = 0;

		if (multi_core_platform_is_connected_to_internet()) {

			SceUserServiceUserId userId = GetNetworkPlayerID();
			uint32_t count = nGetFriendCount(userId);
			int userIndex = nFindIndexOfUserId(userId);
			const SceNpId *friendlistptr = GetFriendSceNpId(0, userId);		// HVS_JRS 10/16/2014 always use index 0 pointing to the first friend as we will index friendlistptr below
			const sce::Toolkit::NP::PresenceInfo *presenceinfoptr;

			static int nTimeLast = 0;										// HVS_JRS 10/6/2014 spread out friends list requests
			int nTimeNow = timer_get(TIMER_MILLISECONDS);

			if (nTimeNow >= (nTimeLast + 5000))
			{
				nTimeLast = nTimeNow;
extern void vQueryFriendsList(SceUserServiceUserId userId);
				vQueryFriendsList(SCE_USER_SERVICE_USER_ID_INVALID);		// HVS_JRS 10/3/2015 update friends list in context
			}

			if ((userIndex>=0 && userIndex<SCE_USER_SERVICE_MAX_LOGIN_USERS) && (friendlistptr != NULL)) {

				// Searching past the end
				if ((int)count <= Current_offset) {
					Result_count = 0;
				} else {
					Result_count_total = 0;

					uint32_t i = 0;
					for (i = 0; i < count; i++) {
				
						uint friend_state = 0;

#if 0	// HVS_JRS 3/6/2014 for now
						friend_state = Friend_list_filter;
#else
				// HVS_JRS 3/6/2014 TODO set friend_state...
						// MULTI_FRIEND_FLAG_ONLINE if online
						// MULTI_FRIEND_FLAG_IN_GAME if playing this game
						// MULTI_FRIEND_FLAG_JOINABLE if joinable
						presenceinfoptr = GetFriendPresenceInfo(i, userId);
						if (presenceinfoptr)
						{
							if (presenceinfoptr->onlineStatus == SCE_NP_GAME_PRESENCE_STATUS_ONLINE)
							{
								friend_state |= MULTI_FRIEND_FLAG_ONLINE;
							}
// HVS_JRS 9/18/2014 if presence info is available, it is only found if friend is in context (same title id)
//							if (presenceinfoptr->gameInfo.npTitleId == np_title_id.id)
//							{
								friend_state |= MULTI_FRIEND_FLAG_IN_GAME;
//							}
							if (presenceinfoptr->gameInfo.gameData[0] & PS4_PRESENCE_SESSION_FLAG_JOINABLE)
							{
								friend_state |= MULTI_FRIEND_FLAG_JOINABLE;	//for now
							}
						}
#endif
						SceNpId info;
						memcpy(&info, &friendlistptr[i], sizeof(info));

						if ((friend_state & Friend_list_filter) == Friend_list_filter) {

							if (Result_count_total >= Start_index) {
								if (Get_list_cb != NULL) {

									// PJA:
									// The online ID can be between 3 and 16 bytes long. The SceNpOnlineId.data field
									// is 16 bytes long. If the online ID is less than 16 bytes long, then the null
									// terminator is inluded in onlineId.data. If it is exactly 16 bytes long, the
									// null terminator is in SceNpOnlineId.term, immediately following.
				// HVS_JRS 3/6/2014 TODO copy actual friends name...
									int copylen = PLAYER_NAME_LENGTH;
									if (copylen > SCE_NP_ONLINEID_MAX_LENGTH+1)	// HVS_JRS 8/13/2014 make sure we copy the null terminator
									{
										copylen = SCE_NP_ONLINEID_MAX_LENGTH+1;
									}
									strncpy_safe( Friend_list_out[Result_count].player_name, info.handle.data, copylen );
									Friend_list_out[Result_count].player_uid.set( info );
									Friend_list_out[Result_count].status_flags = friend_state;

									Friend_list_out[Result_count].presence[0] = 0;
				// HVS_JRS 3/20/2014 copy presence text...
									const char *str = presenceinfoptr->gameInfo.gameStatus.c_str();
									if (str)
									{
										mbstowcs(Friend_list_out[Result_count].presence, str, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
									}
//									//strncpy_safe(Friend_list_out[Result_count].presence, details.status, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
//									mbstowcs(Friend_list_out[Result_count].presence, details.status, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
									Result_count++;
								} else if (Get_uid_list_cb != NULL) {
									Uid_list_out[Result_count].set( info );
									Result_count++;
								} else if (Check_friend_cb != NULL) {
									int j = 0;
									for (j = 0; j < Max_count; j++) {
										if (Uid_list_out[j].is_equal( info )) {
											// We only increase the Result_count if I found my friend
											Are_friends[j] = true;
											Result_count++;
											break;
										}
									}
								} else {
									Result_count++;
								}
							}
							
							Result_count_total++;
						}

						if (Result_count >= Max_count) {
							// And we're done
							break;
						}
					}
				}
			}
		
			if (Get_uid_list_cb) {
				Get_uid_list_cb( Uid_list_out, Result_count, Search_user_data );
			} else if (Get_list_cb) {
				Get_list_cb( Friend_list_out, Result_count, Search_user_data );
			} else if (Check_friend_cb) {
				Check_friend_cb( Uid_list_out, Are_friends, Search_user_data );	
			}

			return true;

		} else {
			return false;
		}
	}

	// Start a join, this is pretty much the same as search, except I'm going to loop through all my friends, and join the player
	// that matches pfriend, if I don't find one, I'm going to fail the join
	//
	static bool start_join( multi_game_friend *pfriend )
	{
		if (multi_core_platform_is_connected_to_internet()) {

			SceUserServiceUserId userId = GetNetworkPlayerID();
			uint32_t count = nGetFriendCount(userId);
			int userIndex = nFindIndexOfUserId(userId);
			const SceNpId *friendlistptr = GetFriendSceNpId(0, userId);		// HVS_JRS 10/16/2014 always use index 0 pointing to the first friend as we will index friendlistptr below
			const sce::Toolkit::NP::PresenceInfo *presenceinfoptr;

			if (friendlistptr != NULL) {

				Result_count_total = 0;

				uint32_t i = 0;
				for (i = 0; i < count; i++) {

					presenceinfoptr = GetFriendPresenceInfo(i, userId);
					if (presenceinfoptr) {

						if (strcmp(&pfriend->player_uid.get_id_pointer()->handle.data[0], &friendlistptr[i].handle.data[0]) == 0) {

							bool joinable = (presenceinfoptr->gameInfo.gameData[0]  & PS4_PRESENCE_SESSION_FLAG_JOINABLE);	// HVS_JRS 10/6/2014 for bundle, we need a different flag for each title
							if (joinable) {
								// Join them
								multi_core_ps4_set_join_info( (ubyte *)&presenceinfoptr->gameInfo.gameData[2], presenceinfoptr->gameInfo.gameData[1] );	// HVS_JRS 3/10/2014 save length in 2nd byte
								return true;
							} else {
								break;
							}
						}
					} // Got info
				} // Loop for friends
			} // Got friends
		} // Connected to internet

		multi_core_platform_clear_join_info(true);
		return false;
	}

#elif defined(__XBOX3)

	// Get the search results
	static bool start_search()
	{
		Query_in_progress = true;
		Query_operation_type = QUERY_OPERATION_FRIENDS;
		Result_count = 0;

		//HVS_ASL[PRINCE] 04-07-2014: Call the LiveCLR version
		bool started = LiveCLR::multi_game_friend_start_search(TITLEID_THIS_GAME,Friend_list_filter);
		if (!started) {
			Query_in_progress = false;
		}

		return started;

	}

	// Start a join, this is pretty much the same as search, except I'm going to loop through all my friends, and join the player
	// that matches pfriend, if I don't find one, I'm going to fail the join
	//
	static bool start_join( multi_game_friend *pfriend )
	{
//		Query_in_progress = true;
//		Query_operation_type = QUERY_OPERATION_JOIN;
		
		//HVS_ASL[PRINCE] 04-07-2014: Call the LiveCLR version
		// I'm assuming that the filter is the same as the last search so LiveCLR::multi_game_friend_start_join uses the results of the last search
		return LiveCLR::multi_game_friend_start_join(pfriend->player_uid.get_xuid());
	}


	// Finish the search, cleanup and exit
	//
	static void finish_search()
	{
		V_ASSERT_RETURN( Query_in_progress );

		if (Get_list_cb) {
			Get_list_cb( Friend_list_out, Result_count, Search_user_data );
		} else if (Get_uid_list_cb) {
			Get_uid_list_cb( Uid_list_out, Result_count, Search_user_data );
		} else if (Check_friend_cb) {
			Check_friend_cb( Uid_list_out, Are_friends, Search_user_data );
		}

		Query_in_progress = false;
		Query_operation_type = QUERY_OPERATION_NONE;
	}

	// Join has finsihed
	static void finish_join() 
	{
		V_ASSERT_RETURN( Query_in_progress );
		Query_in_progress = false;
		Query_operation_type = QUERY_OPERATION_NONE;
	}
#elif defined(__NX__)

	static bool is_joinable(uint64_t pid) {
		auto it = std::find_if(Matchmake_result.begin(), Matchmake_result.end(), [pid](const nn::nex::FindMatchmakeSessionByParticipantResult& r)
		{
			return r.GetPrincipalID() == pid;
		});
		return it != Matchmake_result.end() && it->GetMatchmakeSession().GetOpenParticipation();
	}

	static uint32 get_gathering_id(uint64_t pid) {
		auto it = std::find_if(Matchmake_result.begin(), Matchmake_result.end(), [pid](const nn::nex::FindMatchmakeSessionByParticipantResult& r)
		{
			return r.GetPrincipalID() == pid;
		});
		return it != Matchmake_result.end() ? it->GetMatchmakeSession().GetID() : INVALID_GATHERINGID;
	}

	static void register_friends(bool filter_joinable) {
		for (int i = 0; i < Friend_list_size; ++i) {
			// Only add starting from the start index and filter joinalbe
			if (Result_count_total >= Start_index && (!filter_joinable || is_joinable(Friend_list[i].GetAccountId().id))) {
				if (Get_list_cb != NULL) {					
					wchar_t wide_name[PLAYER_NAME_LENGTH];					
					mbstowcs(wide_name,	Friend_list[i].GetNickname().name, PLAYER_NAME_LENGTH);
					localization_compress_string(Friend_list_out[Result_count].player_name, PLAYER_NAME_LENGTH, wide_name);
					Friend_list_out[Result_count].gathering_id = get_gathering_id(Friend_list[i].GetAccountId().id);
					Friend_list_out[Result_count].player_uid.set(Friend_list[i].GetAccountId().id);
					Friend_list_out[Result_count].status_flags = Friend_list_filter;
					mbstowcs(Friend_list_out[Result_count].presence, Friend_list[i].GetPresence().GetDescription(), MULTI_GAME_FRIEND_PRESSENCE_SIZE);
					Result_count++;
				}
				else if (Get_uid_list_cb != NULL) {
					Uid_list_out[Result_count].set(Friend_list[i].GetAccountId().id);
					Result_count++;
				}
				else if (Check_friend_cb != NULL) {
					for (int j = 0; j < Max_count; j++) {
						if (Uid_list_out[j].is_equal(Friend_list[i].GetAccountId().id)) {
							Are_friends[j] = true;
							Result_count++;
							break;
						}
					}
				}
				else {
					Result_count++;
				}
			}
			Result_count_total++;

			if (Result_count >= Max_count) {
				// And we're done
				break;
			}
		}

		if (Get_uid_list_cb) {
			Get_uid_list_cb(Uid_list_out, Result_count, Search_user_data);
		}
		else if (Get_list_cb) {
			Get_list_cb(Friend_list_out, Result_count, Search_user_data);
		}
		else if (Check_friend_cb) {
			Check_friend_cb(Uid_list_out, Are_friends, Search_user_data);
		}

		Query_in_progress = false;
	}

    static bool start_search()
    {
		Result_count = 0;
		Result_count_total = 0;

		// Can't do anything if we ain't connected.
		if (!multi_core_platform_is_connected_to_internet()) {
			return false;
		}

		nn::friends::FriendFilter filter = {};

		if ((Friend_list_filter & MULTI_FRIEND_FLAG_ONLINE) != 0) {
			filter.presenceStatus = nn::friends::PresenceStatusFilter_OnlineOrOnlinePlay;
		}
		if ((Friend_list_filter & MULTI_FRIEND_FLAG_IN_GAME) != 0) {
			filter.isSameAppPresenceOnly = true;
		}

		Friend_list_size = 0;
		nn::Result result = nn::friends::GetFriendList(&Friend_list_size, Friend_list, 0, nn::friends::FriendCountMax, filter);
		if (result.IsFailure()) {
			nn::err::ShowError(result);
			return false;
		}

		if (Friend_list_size > 0 && (Friend_list_filter & MULTI_FRIEND_FLAG_JOINABLE) != 0) {
			V_ASSERT(Matchmake_ctx.GetState() == nn::nex::CallContext::Available);
			if (Matchmake_ctx.GetState() != nn::nex::CallContext::Available) {
				// TODO apparently we should log out
				return false;
			}

			int nsa_ids_size = std::min(Friend_list_size, static_cast<int>(nn::nex::MAX_PRINCIPALID_SIZE_TO_FIND_MATCHMAKE_SESSION));
			nn::account::NetworkServiceAccountId nsa_ids[nsa_ids_size];
			for (int i = 0; i < nsa_ids_size; ++i) {
				nsa_ids[i] = Friend_list[i].GetAccountId();
			}
			Matchmake_result.clear();
			nn::nex::FindMatchmakeSessionByParticipantParam params;
			params.SetPrincipalIdListByNetworkServiceAccountId(nsa_ids, nsa_ids_size);
			if (!Matchmaking_client->FindMatchmakeSessionByParticipant(&Matchmake_ctx, params, &Matchmake_result)) {
				V_ASSERT(false); // should never happen
				return false;
			}
			Query_in_progress = true;
		}
		else {
			register_friends(false);
		}

		return true;
    }

    static bool start_join(multi_game_friend *pfriend)
    {
		// Can't do anything if we ain't connected.
		if (!multi_core_platform_is_connected_to_internet()) {
			multi_core_platform_clear_join_info(true);
			return false;
		}

		if (pfriend != NULL && pfriend->player_uid.is_valid() && pfriend->gathering_id != INVALID_GATHERINGID) {
			multi_core_nx64_set_join_info(pfriend->gathering_id, pfriend->player_uid.get_nsa_id(), My_player_uid.get_nsa_id());
			return true;
		}
		else {
			multi_core_platform_clear_join_info(true);
			return false;
		}
    }
#elif defined(__PC_GP)
// Get the search results
	static bool start_search()
	{
		live_friend_search_profile_state = LIVE_QUERY_OP_IDLE;
		live_friend_search_presence_state = LIVE_QUERY_OP_IDLE;
		live_friend_serach_activities_state = LIVE_QUERY_OP_IDLE;
		live_friends_user_ids.clear();
		live_friends_profiles.clear();
		live_friends_presence_records.clear();
		live_friends_activities.clear();
		Result_count = 0;
		Result_count_total = 0;

		XAsyncBlock* presence_async_block = live_create_new_async_block();
		presence_async_block->callback = [](XAsyncBlock* async) {
			size_t presenceCount;
			HRESULT hr = XblPresenceGetPresenceForSocialGroupResultCount(async, &presenceCount);
			if (SUCCEEDED(hr) && presenceCount > 0) {
				//Array of handles to populate.These handles will later need to be released with XblPresenceRecordCloseHandle when they are no longer needed.
				live_friends_presence_records.resize(presenceCount);
				hr = XblPresenceGetPresenceForSocialGroupResult(async, live_friends_presence_records.data(), presenceCount);
				if (FAILED(hr)) {
					live_friends_presence_records.clear();
				}
			}
			live_friend_search_presence_state = LIVE_QUERY_OP_COMPLETED;
			live_delete_async_block(async);
		};
		// Currently we always get the Xbox-App as title. In that case we do not apply any filter
		live_friend_search_presence_state = LIVE_QUERY_OP_ACTIVE;

		XblGuid scid;

		live_friend_serach_activities_state = LIVE_QUERY_OP_ACTIVE;

		Query_in_progress = true;

		return true;
	}

	// Start a join, this is pretty much the same as search, except I'm going to loop through all my friends, and join the player
	// that matches pfriend, if I don't find one, I'm going to fail the join
	//
	static bool start_join( multi_game_friend *pfriend )
	{
		// Can't do anything if we ain't connected.
		if (!multi_core_platform_is_connected_to_internet()) {
			multi_core_platform_clear_join_info(true);
			return false;
		}

		if (pfriend == NULL || !pfriend->player_uid.is_valid()) {
			multi_core_platform_clear_join_info(true);
			return false;
		}

		multi_core_pcgp_set_join_info(pfriend->session_id, pfriend->handle_id, pfriend->player_uid.get_xuid(), My_player_uid.get_xuid());

		return true;
	}


	// Finish the search, cleanup and exit
	//
	static void finish_search()
	{
		V_ASSERT_RETURN(Query_in_progress);

		if (Get_list_cb) {
			Get_list_cb(Friend_list_out, Result_count, Search_user_data);
		}
		else if (Get_uid_list_cb) {
			Get_uid_list_cb(Uid_list_out, Result_count, Search_user_data);
		}
		else if (Check_friend_cb) {
			Check_friend_cb(Uid_list_out, Are_friends, Search_user_data);
		}

		Query_in_progress = false;
	}

	// Join has finsihed
	static void finish_join() 
	{
		V_ASSERT_RETURN(Query_in_progress);
		Query_in_progress = false;
	}
#endif

static void enqueue_invite( multi_game_friend_invite **queue, multi_game_friend_invite *invite );
static multi_game_friend_invite* dequeue_invite( multi_game_friend_invite **queue );

// Resets the invites and cancels any current one in progress
//
static void reset_invites()
{
	#if defined(__LIVE_ENABLED)
		if (Inviting && !XHasOverlappedIoCompleted(&Invite_operation)) {
			XCancelOverlapped(&Invite_operation);
		}	
		memset( &Invite_operation, 0, sizeof(XOVERLAPPED) );
	#endif

	Inviting = false;

	Current_invites = NULL;
	Free_invites = NULL;
	
	// Put them on the free list
	//
	memset( Pending_invites, 0, sizeof(Pending_invites) );
	for (int i = 0; i < MULTI_GAME_FRIEND_MAX_PENDING_INVITES; i++) {
		enqueue_invite( &Free_invites, &(Pending_invites[i]) );
	}

	Invites_sent.remove_all();
}

// Resets query information, and stops the current operation
//
static void reset_query()
{
	#if defined(__LIVE_ENABLED)
		if (Query_in_progress && !XHasOverlappedIoCompleted(&Query_operation)) {
			XCancelOverlapped(&Query_operation);
		}
		memset( &Query_operation, 0, sizeof(XOVERLAPPED) );

		if (Query_handle != INVALID_HANDLE_VALUE) {
			CloseHandle( Query_handle );
			Query_handle = INVALID_HANDLE_VALUE;
		}
	#elif defined(__NX__)
		Friend_list_size = 0;
		Matchmake_result.clear();

		if (Matchmake_ctx.GetState() == nn::nex::CallContext::CallInProgress) {
			Matchmake_ctx.Cancel();
		}
		Matchmake_ctx.Reset();
	#elif defined(__PC_GP)
		live_friend_search_profile_state = LIVE_QUERY_OP_IDLE;
		live_friend_search_presence_state = LIVE_QUERY_OP_IDLE;
		live_friend_serach_activities_state = LIVE_QUERY_OP_IDLE;
		live_friends_profiles.clear();
		live_friends_presence_records.clear();
		live_friends_activities.clear();
		live_friends_user_ids.clear();
	#endif 

	// Call the callback and since it failed, just return a list of 0 friends
	if (Query_in_progress) {
		if (Get_list_cb) {
			Get_list_cb( Friend_list_out, 0, Search_user_data );
		} else if (Get_uid_list_cb) {
			Get_uid_list_cb( Uid_list_out, 0, Search_user_data );
		} else if (Check_friend_cb != NULL) {
			Check_friend_cb( Uid_list_out, Are_friends, Search_user_data );
		}
	}

	Query_in_progress = false;

	// Reset all the search data
	Query_in_progress = false;
	Friend_list_out = NULL;
	Uid_list_out = NULL;
	Max_count = 0;
	Search_user_data = NULL;
	Result_count = 0;
	Current_offset = 0;
}

// I like writing queues... this is probably going to buggy, why do I do this to myself?
//
static void enqueue_invite( multi_game_friend_invite **queue, multi_game_friend_invite *invite )
{
	if (*queue == NULL) {
		*queue = invite;
	} else {

		// Put it at the end
		multi_game_friend_invite *back = *queue;
		while (back->prev != NULL) {
			back = back->prev;
		}

		back->prev = invite;
	}

	invite->prev = NULL;
}

// Same comment as above
//
static multi_game_friend_invite* dequeue_invite( multi_game_friend_invite **queue )
{
	if (*queue != NULL) {
		multi_game_friend_invite *ret = *queue;
		*queue = (*queue)->prev;

		ret->prev = NULL;
		return ret;
	} else {
		return NULL;
	}
}

// Send the next invite in the list
//
static void send_next_invite()
{
	V_ASSERT( Inviting == false );

	CEG_TEST_SECRET();

	multi_session *sp = multi_session_get_joinable_session();
	
	multi_game_friend_invite *current_invite = Current_invites;
	while (current_invite && !Inviting) {
		if (multi_core_platform_is_connected_to_internet() && sp && sp->can_i_invite() && !sp->get_options()->systemlink_or_lan) {

			// Platform specific mumbo_jumbo here
			#if defined(__LIVE_ENABLED)
				DWORD result = XInviteSend( multi_core_platform_get_active_controller(), 1, 
					current_invite->friend_ptr->player_uid.get_id_pointer(), 
					current_invite->message, 
					&Invite_operation );

				if (result == ERROR_IO_PENDING) {
					Inviting = true;
				} else {
					memset( &Invite_operation, 0, sizeof(XOVERLAPPED) );
					// This will quit eventually since I have a limited number of invites
					dequeue_invite( &Current_invites );					// remove it from the list
					enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
					current_invite = Current_invites;
				}

			#elif defined(__XBOX3)
				multi_core_platform_send_invite( current_invite->friend_ptr->player_uid, current_invite->message, sp );
				dequeue_invite( &Current_invites );					// remove it from the list
				enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
				current_invite = Current_invites;

			#elif defined(__PS3)

				multi_core_platform_send_invite( current_invite->friend_ptr->player_uid, current_invite->message, sp );
				// I don't care if it succeeded or not... just going to assume it did... if the invite didn't go, they'll send it again
					
				dequeue_invite( &Current_invites );					// remove it from the list
				enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
				current_invite = Current_invites;

			#elif defined(__PS4)	// HVS_JRS 3/11/2014 ps4 networking invites

				multi_core_platform_send_invite( current_invite->friend_ptr->player_uid, current_invite->message, sp );
				// I don't care if it succeeded or not... just going to assume it did... if the invite didn't go, they'll send it again
					
				dequeue_invite( &Current_invites );					// remove it from the list
				enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
				current_invite = Current_invites;

			#elif defined(__STEAM)
				multi_core_platform_send_invite( current_invite->friend_ptr->player_uid, current_invite->message, sp );
				dequeue_invite( &Current_invites );					// remove it from the list
				enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
				current_invite = Current_invites;

			//#elif defined(__PC_GP)
			//	multi_core_platform_send_invite(current_invite->friend_ptr->player_uid, current_invite->message, sp);
			//	dequeue_invite(&Current_invites);					// remove it from the list
			//	enqueue_invite(&Free_invites, current_invite);	// Add it to the free list
			//	current_invite = Current_invites;

			#else
				V_ERROR_MSG( "!HERE BE DRAGONS!" ); //  You should probably fill this in for you platform
				dequeue_invite( &Current_invites );					// remove it from the list
				enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
				current_invite = Current_invites;
			#endif

		} else {
			// This will quit eventually since I have a limited number of invites
			dequeue_invite( &Current_invites );					// remove it from the list
			enqueue_invite( &Free_invites, current_invite );	// Add it to the free list
			current_invite = Current_invites;
		}
	}

}

#if defined(__LIVE_ENABLED) 
	static void process_invite()
	{
		// Am I done inviting?
		if (Inviting && XHasOverlappedIoCompleted(&Invite_operation)) {
			// Clear out the current invite and send the next one out if it exists
			memset( &Invite_operation, 0, sizeof(XOVERLAPPED) );
			enqueue_invite( &Free_invites, dequeue_invite( &Current_invites ) );
			Inviting = false;
			send_next_invite();
		}
	}

	// Process the current search
	static void process_search()
	{
		if (Query_in_progress && (Query_operation_type == QUERY_OPERATION_FRIENDS) && XHasOverlappedIoCompleted(&Query_operation)) {

			DWORD ret, result;
			ret = XGetOverlappedResult( &Query_operation, &result, FALSE );

			if (ret != ERROR_SUCCESS) {
				Result_count = 0;
				finish_search();
			} else {

				// Return the proper results
				DWORD count = ((DWORD)(Query_operation.InternalHigh));
				register_friends( (int)count );

				// Okay, I need to know if I'm done yet
				// Either I didn't return all that I could, or I'm done searching, finish
				if ((count < MAX_QUERIES_AT_ONCE) || !continue_search()) {
					finish_search(); 
				}
			}
		}
	}

	// Process the join operation - really, this is a search operation, but let's not get ahead of ourselves
	//
	static void process_join()
	{
		if (Query_in_progress && (Query_operation_type == QUERY_OPERATION_JOIN) && XHasOverlappedIoCompleted(&Query_operation)) {

			DWORD ret, result;
			ret = XGetOverlappedResult( &Query_operation, &result, FALSE );

			if (ret != ERROR_SUCCESS) {
				multi_core_platform_clear_join_info( true );	// This clears all joins... which is fine, since if they got to this point it means they just tried to join a new game
				finish_join();
			} else {
				XSESSION_SEARCHRESULT_HEADER *results = (XSESSION_SEARCHRESULT_HEADER*)Query_buffer;
				if (results->dwSearchResults == 0) {
					multi_core_platform_clear_join_info( true );
				} else {
					V_ASSERT( results->dwSearchResults == 1 );
					multi_core_platform_live_set_pending_join_info( results->pResults[0].info, My_player_uid.get_id(), false );
				}
				finish_join();
			}
		}
	}

#elif defined(__PS3) 
	static void process_invite() {};
	static void process_search() {};
	static void process_join() {};	// Man PS3 is easy when it comes to friends

#elif defined(__STEAM)

	static void process_invite()
	{
	}

	static void process_search()
	{
	}

	static void process_join()
	{
	}

#elif defined(__PS4)	// HVS_JRS 3/6/2014 add friends list

	static void process_invite() {};
	static void process_search() {};
	static void process_join() {};	// Man PS4 is easy when it comes to friends

#elif defined(__XBOX3) // HVS_MMK[PRINCE] 01/13/2014

	static void process_invite()
	{
#if live_version
		// Am I done inviting?
		if (Inviting && XHasOverlappedIoCompleted(&Invite_operation)) {
			// Clear out the current invite and send the next one out if it exists
			memset( &Invite_operation, 0, sizeof(XOVERLAPPED) );
			enqueue_invite( &Free_invites, dequeue_invite( &Current_invites ) );
			Inviting = false;
			send_next_invite();
		}
#endif
	}

//HVS_ASL[KING] 12-11-2014: Sorting function for the invite list.
#define DO_INVITE_LIST_SORT_DEBUG_OUTPUT	//HVS_ASL[KING] 12-11-2014: Enable this to get extra debug info for the sort process.
#if defined(__XBOX3)
	static bool SortXboxClientInfoForInviteList(const LiveCLR::XboxClientInfo &a, const LiveCLR::XboxClientInfo &b)
	{
		bool put_a_above_b = true;
		uint a_is_active = (a.player_status & MULTI_FRIEND_FLAG_IN_GAME);
		uint b_is_active = (b.player_status & MULTI_FRIEND_FLAG_IN_GAME);

		// Sort the in-game players at the top
		if( b_is_active > a_is_active )
		{
			put_a_above_b = false;
		}
		else if ( b_is_active == a_is_active )	// Then alphabetically in ascending order
		{
			put_a_above_b = (strcmp(a.player_name,b.player_name) < 0);
		}

		return put_a_above_b;
	}
#endif

	static void process_search()
	{
		if (Query_in_progress && LiveCLR::multi_game_friend_is_search_done())
		{
			if (Query_operation_type == QUERY_OPERATION_FRIENDS)
			{
#if defined(__XBOX3)
#if defined(DO_INVITE_LIST_SORT_DEBUG_OUTPUT)
				// print names before sort
				release_printf("\nBefore sort\n");
				int i = 0;
				for(LiveCLR::XboxClientInfo search_result : LiveCLR::friend_search_results)
				{
					release_printf("%d] %08X \"%ls\"\n", i, search_result.player_status, search_result.player_name);
					i++;
				}
#endif
				std::sort(LiveCLR::friend_search_results.begin(),LiveCLR::friend_search_results.end(),SortXboxClientInfoForInviteList);

#if defined(DO_INVITE_LIST_SORT_DEBUG_OUTPUT)
				// print names after sort
				release_printf("\nAfter sort\n");
				i = 0;
				for(LiveCLR::XboxClientInfo search_result : LiveCLR::friend_search_results)
				{
					release_printf("%d] %08X \"%ls\"\n", i, search_result.player_status, search_result.player_name);
					i++;
				}
#endif
#endif
				for(LiveCLR::XboxClientInfo search_result : LiveCLR::friend_search_results)
				{
					// Copy the name
					strncpy_safe(Friend_list_out[Result_count].player_name,search_result.player_name,PLAYER_NAME_LENGTH);
					Friend_list_out[Result_count].player_name[PLAYER_NAME_LENGTH-1] = NULL;	//HVS_ASL[PRINCE] 04-08-2014: Just to make sure since PLAYER_NAME_LENGTH was based on gamertags not MSA names.
					// Copy the presence
					strncpy_safe(Friend_list_out[Result_count].presence,search_result.player_presence,MULTI_GAME_FRIEND_PRESSENCE_SIZE);
					Friend_list_out[Result_count].presence[MULTI_GAME_FRIEND_PRESSENCE_SIZE-1] = NULL;	//HVS_ASL[PRINCE] 04-08-2014: Just to make sure since PLAYER_NAME_LENGTH was based on gamertags not MSA names.
					// Copy the uid
					Friend_list_out[Result_count].player_uid.set(search_result.player_uid);

					// Copy the status
					Friend_list_out[Result_count].status_flags = search_result.player_status;

					// Copy the session id
					Friend_list_out[Result_count].session_id = search_result.session_uid;

					Result_count++;
				}
				finish_search();
			}
			else if (Query_operation_type == QUERY_OPERATION_JOIN)
			{

			}
#if live_version // register_friends
			XONLINE_FRIEND *friends = reinterpret_cast<XONLINE_FRIEND*>(Query_buffer);

			for (int i = 0; i < count; i++) {

				uint friend_state = 0;

				bool request_pending = (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST) || (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST);
				if (request_pending) {
					continue;
				}

				if ( (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_ONLINE) ) {

					friend_state |= MULTI_FRIEND_FLAG_ONLINE;
				}

				if (friend_state & MULTI_FRIEND_FLAG_ONLINE) {

					// If they're not on-line, I don't care about them.
					if (friends[i].dwTitleID == TITLEID_THIS_GAME) {
						friend_state |= MULTI_FRIEND_FLAG_IN_GAME;
					}

					if ((friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_JOINABLE) || (friends[i].dwFriendState & XONLINE_FRIENDSTATE_FLAG_JOINABLE_FRIENDS_ONLY)) {
						// Accept joinable for friends only since... well, I'm getting my friends list aren't I?
						friend_state |= MULTI_FRIEND_FLAG_JOINABLE;
					}
				}

				// Check if they meet ALL of our filters
				if ((friend_state & Friend_list_filter) == Friend_list_filter) {

					if (Result_count_total >= Start_index) {
						if (Get_list_cb != NULL) {
							strncpy_safe(Friend_list_out[Result_count].player_name, friends[i].szGamertag, PLAYER_NAME_LENGTH);
							Friend_list_out[Result_count].player_uid.set( friends[i].xuid );
							Friend_list_out[Result_count].status_flags = friend_state;
							Friend_list_out[Result_count].session_id = friends[i].sessionID;

							Friend_list_out[Result_count].presence[0] = 0;
							if (friends[i].cchRichPresence > 0) {
								strncpy_safe(Friend_list_out[Result_count].presence, friends[i].wszRichPresence, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
							} 

						} else if (Get_uid_list_cb) {
							Uid_list_out[Result_count].set( friends[i].xuid );
						} else if (Check_friend_cb) {
							int j = 0;
							for (j = 0; j < Max_count; j++) {
								if (Uid_list_out[j].is_equal(friends[i].xuid)) {
									break;
								}
							}

							if (j < Max_count) {
								Are_friends[j] = true;
							}
						}

						Result_count++;
					}

					Result_count_total++;
				}
			}
#endif
		}
	}

	static void process_join()
	{
	}

#elif defined(__NX__)

	static void process_invite()
	{
	}

	static const int32 min_search_delay = 12000;
	static int32 last_search_time = 0;

	static void process_search()
	{
		if (Query_in_progress) {
			int32 now = timer_get_milliseconds();
			int32 diff = now - last_search_time;
			if (last_search_time > 0 && diff < min_search_delay)
			{
				return;
			}

			switch (Matchmake_ctx.GetState()) {
			case nn::nex::CallContext::CallInProgress:
				// just wait
				break;
			case nn::nex::CallContext::CallFailure:
			case nn::nex::CallContext::CallCancelled:
				nn::err::ShowError(Matchmake_ctx.GetOutcome().GetErrorCodeStruct());
				// Fall-through intentional
			case nn::nex::CallContext::CallSuccess:
				last_search_time = timer_get_milliseconds();
				register_friends(true);
				Matchmake_ctx.Reset();
				break;
			default:
				// invalid state
				V_ASSERT_MSG(false, "Invalid state");
			}
		}
	}

	static void process_join()
	{
	}

#elif defined(__PC_GP)
	static void process_invite()
	{
	}

	static void process_search()
	{
		if (!Query_in_progress) {
			return;
		}

		if (live_friend_search_presence_state == LIVE_QUERY_OP_COMPLETED && live_friend_search_profile_state == LIVE_QUERY_OP_IDLE) {
			for (auto precence_record_it = live_friends_presence_records.begin(); precence_record_it != live_friends_presence_records.end(); ++precence_record_it) {
				XblPresenceRecordHandle precence_record = *precence_record_it;

				uint64_t friend_xuid;
				HRESULT hr = XblPresenceRecordGetXuid(precence_record, &friend_xuid);
				if (FAILED(hr)) {
					continue;
				}

				XblPresenceUserState userState;
				hr = XblPresenceRecordGetUserState(precence_record, &userState);
				if (FAILED(hr)) {
					continue;
				}

				if (userState != XblPresenceUserState::Online) {
					continue;
				}

				live_friends_user_ids.push_back(friend_xuid);
			}

			if (live_friends_user_ids.size() > 0) {
				XAsyncBlock* asyncBlock = live_create_new_async_block();
				asyncBlock->callback = [](XAsyncBlock* asyncBlock) {
					uint32_t profileCount;
					live_friend_search_profile_state = LIVE_QUERY_OP_COMPLETED;
					live_delete_async_block(asyncBlock);
				};

				HRESULT hr = XblProfileGetUserProfilesAsync(Live_context, live_friends_user_ids.data(), live_friends_user_ids.size(), asyncBlock);
				live_friend_search_profile_state = LIVE_QUERY_OP_ACTIVE;
				if (FAILED(hr)) {
					live_friend_search_profile_state = LIVE_QUERY_OP_COMPLETED;
				}

				XblGuid scid;
				
			
			}
			else {
				live_friend_search_profile_state = LIVE_QUERY_OP_COMPLETED;
			}
		}

		if (live_friend_search_profile_state == LIVE_QUERY_OP_COMPLETED &&
			live_friend_search_presence_state == LIVE_QUERY_OP_COMPLETED &&
			live_friend_serach_activities_state == LIVE_QUERY_OP_COMPLETED) {
			
			live_friend_search_profile_state = LIVE_QUERY_OP_IDLE;
			live_friend_search_presence_state = LIVE_QUERY_OP_IDLE;
			live_friend_serach_activities_state = LIVE_QUERY_OP_IDLE;

			// DSFL-TODO: Do we need to sort?
#if defined(__XBOX3)
			std::sort(LiveCLR::friend_search_results.begin(), LiveCLR::friend_search_results.end(), SortXboxClientInfoForInviteList);
#endif
			uint32_t titleId = live_get_title_id();

			for (auto precence_record_it = live_friends_presence_records.begin(); precence_record_it != live_friends_presence_records.end(); ++precence_record_it) {
				XblPresenceRecordHandle precence_record = *precence_record_it;

				uint friend_status = MULTI_FRIEND_FLAG_NONE;

				uint64_t xuid;
				HRESULT hr = XblPresenceRecordGetXuid(precence_record, &xuid);
				if (FAILED(hr)) {
					continue;
				}
				multi_player_uid friend_uid;
				friend_uid.set(xuid);

				// Do we need this state if we set the only online users filter
				XblPresenceUserState userState;
				hr = XblPresenceRecordGetUserState(precence_record, &userState);
				if (FAILED(hr)) {
					continue;
				}

				if (userState == XblPresenceUserState::Online) {
					friend_status |= MULTI_FRIEND_FLAG_ONLINE;
				}

				const XblPresenceDeviceRecord* deviceRecords;
				size_t deviceRecordsCount;
				hr = XblPresenceRecordGetDeviceRecords(precence_record, &deviceRecords, &deviceRecordsCount);
				if (FAILED(hr)) {
					continue;
				}

				char presence[MULTI_GAME_FRIEND_PRESSENCE_SIZE] = "";
				// I need only the last one. Not the whole record history!!!
				for (int j = 0; j < deviceRecordsCount; ++j, ++deviceRecords) {
					const XblPresenceTitleRecord* titleRecord = deviceRecords->titleRecords;
					for (int k = 0; k < deviceRecords->titleRecordsCount; ++k, ++titleRecord) {
						if (titleRecord->titleId == titleId && titleRecord->titleActive) {

							if (titleRecord->viewState == XblPresenceTitleViewState::FullScreen) {
								friend_status |= MULTI_FRIEND_FLAG_IN_GAME;
							}
							strncpy_safe(&presence[0], titleRecord->richPresenceString, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
							break;
						}
					}
				}

				// find user
				XblUserProfile* found_friend_profile = nullptr;
				for (auto friend_profile_it = live_friends_profiles.begin(); friend_profile_it != live_friends_profiles.end(); ++friend_profile_it) {
					if (friend_profile_it->xboxUserId == friend_uid.get_xuid()) {
						found_friend_profile = &(*friend_profile_it);
						break;
					}
				}

				if (found_friend_profile == nullptr) {
					continue;
				}

				GUID handle_id;
				CoCreateGuid(&handle_id);
				GUID session_id;
				CoCreateGuid(&session_id);
				for (auto activity_it = live_friends_activities.begin(); activity_it != live_friends_activities.end(); ++activity_it) {
					if (activity_it->OwnerXuid == friend_uid.get_xuid() && activity_it->TitleId == titleId) {
						friend_status |= MULTI_FRIEND_FLAG_IN_GAME;
						if (activity_it->Visibility != XblMultiplayerSessionVisibility::Full &&
							(activity_it->JoinRestriction == XblMultiplayerSessionRestriction::None ||
								activity_it->JoinRestriction == XblMultiplayerSessionRestriction::Followed)) {
							friend_status |= MULTI_FRIEND_FLAG_JOINABLE;
						}
						string_to_guid(activity_it->HandleId, &handle_id);
						string_to_guid(activity_it->SessionReference.SessionName, &session_id);
						break;
					}
				}

				if (Result_count_total >= Start_index) {
					if (Get_list_cb != NULL) {
						strncpy_safe(Friend_list_out[Result_count].player_name, found_friend_profile->gameDisplayName, PLAYER_NAME_LENGTH);
						Friend_list_out[Result_count].player_name[PLAYER_NAME_LENGTH - 1] = NULL;
						strncpy_safe(Friend_list_out[Result_count].presence, presence, MULTI_GAME_FRIEND_PRESSENCE_SIZE);
						Friend_list_out[Result_count].presence[MULTI_GAME_FRIEND_PRESSENCE_SIZE - 1] = NULL;
						Friend_list_out[Result_count].player_uid.set(friend_uid);
						Friend_list_out[Result_count].status_flags = friend_status;
						Friend_list_out[Result_count].handle_id = handle_id;
						Friend_list_out[Result_count].session_id = session_id;
						Result_count++;
					}
					else if (Get_uid_list_cb != NULL) {
						Uid_list_out[Result_count].set(friend_uid);
						Result_count++;
					}
					else if (Check_friend_cb != NULL) {
						for (int j = 0; j < Max_count; j++) {
							if (Uid_list_out[j].is_equal(friend_uid)) {
								Are_friends[j] = true;
								Result_count++;
								break;
							}
						}
					}
				}
				Result_count_total++;
				if (Result_count >= Max_count) {
					// And we're done
					break;
				}
			}

			for (auto precence_record_it = live_friends_presence_records.begin(); precence_record_it != live_friends_presence_records.end(); ++precence_record_it) {
				XblPresenceRecordCloseHandle(*precence_record_it);
			}

			live_friends_profiles.clear();
			live_friends_presence_records.clear();
			live_friends_activities.clear();
			live_friends_user_ids.clear();
			
			finish_search();
		}
	}

	static void process_join()
	{
	}
#endif

static void multi_game_friend_mark_invite_sent( multi_game_friend *pfriend )
{
	// Already have an entry for this friend? Just update it.
	for (int i = 0; i < Invites_sent.size(); i++) {
		if (Invites_sent[i].uid.is_equal(pfriend->player_uid)) {
			Invites_sent[i].sent_ts.set(INVITE_TIMER_MS);
			return;
		}
	}

	if (Invites_sent.full()) {
		Invites_sent.remove(0); // Drop the first element.
	}

	multi_game_friend_invite_sent invite;

	invite.uid = pfriend->player_uid;
	invite.sent_ts.set(INVITE_TIMER_MS);
	Invites_sent.add(invite);
}

// --------------------
//
// External Functions
//
// --------------------

bool multi_game_friend_can_send_invite( multi_game_friend *pfriend )
{
	// Look for the sent invite with this friend's uid. If the timer has not yet elapsed, don't 
	// allow us to send another.
	for (int i = 0; i < Invites_sent.size(); i++) {
		if (Invites_sent[i].uid.is_equal(pfriend->player_uid) && !Invites_sent[i].sent_ts.elapsed()) {
			return false;
		}
	}

	return true;
}

// Process current operations in process
void multi_game_friend_process()
{
	process_invite();
	process_search();
	process_join();
}

// Cancel all searches
//
void multi_game_friend_cancel_all()
{
	reset_query();
	reset_invites();
}

// Send the invite out.
//
multi_invite_result multi_game_friend_send_invite( multi_game_friend *pfriend, const TCHAR *invite_message )
{
	if (!multi_core_platform_is_connected_to_internet()) {
		return MULTI_INVITE_FAIL_INTERNAL;
	}

	// Do not invite people already in this session
	multi_session *sp = multi_session_get_joinable_session();
	if (sp == NULL) {
		return MULTI_INVITE_FAIL_INTERNAL;
	}

	multi_connection *cp = NULL;
	LLP_FOR_LOOP( cp, sp->get_connection_list() ) {
		if (cp->player_uid.is_equal(pfriend->player_uid)) {
			return MULTI_INVITE_FAIL_ALREADY_IN_MATCH;
		}
	}

	multi_game_friend_invite *invite = dequeue_invite( &Free_invites );
	if (invite != NULL) {

		invite->friend_ptr = pfriend;
		invite->message = invite_message;
		enqueue_invite( &Current_invites, invite );

		if (!Inviting) {
			send_next_invite();
		}

		multi_game_friend_mark_invite_sent(pfriend);

		return MULTI_INVITE_SUCCESS;

	} else {
		return MULTI_INVITE_FAIL_INTERNAL;
	}
}

// Get a list of my friends
//
bool multi_game_friend_get_list( multi_game_friend *friends, uint filter, int starting_index, int max_friends, multi_game_friend_get_list_cb cb, void *user_data )
{
	V_ASSERT_RETURN_VALUE( cb != NULL, false );

	if (Query_in_progress) {
		debug_printf("mp_multi", "multi_game_friend_get_list Operation is already in progress. Ignoring new request." );
		return false;
	}

	if (!multi_core_platform_is_connected_to_internet()) {
		V_ASSERT( !"Not logged into the internet, can't search for friends." );
		return false;
	}

	// Setup the starting variables
	Friend_list_out = friends;
	Uid_list_out = NULL;

	Friend_list_filter = filter;

	Max_count = max_friends;
	Start_index = starting_index;
	Current_offset = 0;
	Get_list_cb = cb;
	Get_uid_list_cb = NULL;
	Check_friend_cb = NULL;
	Search_user_data = user_data;

	return start_search();
}

// Get my friends, but only their UIDs, this is usually used by other modules like leader boards
//
bool multi_game_friend_get_list( multi_player_uid *uids, uint filter, int starting_index, int max_friends, multi_game_friend_get_uid_list_cb cb, void *user_data )
{
	V_ASSERT_RETURN_VALUE( cb != NULL, false );

	if (Query_in_progress) {
		debug_printf("mp_multi", "multi_game_friend_get_list Operation is already in progress. Ignoring new request." );
		return false;
	}

	if (!multi_core_platform_is_connected_to_internet()) {
		V_ASSERT( !"Not logged into the internet, can't search for friends." );
		return false;
	}

	// Setup the starting variables
	Friend_list_out = NULL;
	Uid_list_out = uids;

	Friend_list_filter = filter;

	Max_count = max_friends;
	Start_index = starting_index;
	Current_offset = 0;
	Get_list_cb = NULL;
	Get_uid_list_cb = cb;
	Check_friend_cb = NULL;
	Search_user_data = user_data;

	return start_search();
}

bool multi_game_friend_check( multi_player_uid *uids, int num_friends, multi_game_friend_check_cb cb, void *user_data )
{
	V_ASSERT_RETURN_VALUE( cb != NULL, false );

	if (Query_in_progress) {
		return false;
	}

	if (num_friends > MAX_CONNECTIONS) {
		V_ASSERT( !"This should not be used for anything except checking people in the match, or checking a single person" );
		return false;
	}

	if (!multi_core_platform_is_connected_to_internet()) {
		return false;
	}

	// Setup the starting variables
	Friend_list_out = NULL;
	Uid_list_out = uids;

	Friend_list_filter = MULTI_FRIEND_FLAG_NONE;

	memset( Are_friends, 0, sizeof(Are_friends) );
	Max_count = num_friends;
	Start_index = 0;
	Current_offset = 0;
	Get_list_cb = NULL;
	Get_uid_list_cb = NULL;
	Check_friend_cb = cb;
	Search_user_data = user_data;

	return start_search();

}

// Join a friend, this will only work with friends who are marked as joinable and have valid join info
// I will set the "join failed" flag on my join info if this actually fails.
//
bool multi_game_friend_join( multi_game_friend *pfriend )
{
	if (Query_in_progress) {
		return false;
	}

	if ((pfriend == NULL) || !(pfriend->status_flags & MULTI_FRIEND_FLAG_JOINABLE)
		|| !(pfriend->status_flags & MULTI_FRIEND_FLAG_IN_GAME) ) {

		return false;
	}

	return start_join( pfriend );
}

#else

// Process current operations in process
void multi_game_friend_process()
{
}

// Cancel all searches
//
void multi_game_friend_cancel_all()
{
}

// Send the invite out.
//
multi_invite_result multi_game_friend_send_invite( multi_game_friend *pfriend, const TCHAR *invite_message )
{
	return MULTI_INVITE_FAIL_INTERNAL;
}

// Get a list of my friends
//
bool multi_game_friend_get_list( multi_game_friend *friends, uint filter, int starting_index, int max_friends, multi_game_friend_get_list_cb cb, void *user_data )
{
	return false;
}

// Get my friends, but only their UIDs, this is usually used by other modules like leader boards
//
bool multi_game_friend_get_list( multi_player_uid *uids, uint filter, int starting_index, int max_friends, multi_game_friend_get_uid_list_cb cb, void *user_data )
{
	return false;
}

bool multi_game_friend_check( multi_player_uid *uids, int num_friends, multi_game_friend_check_cb cb, void *user_data )
{
	return false;
}

// Join a friend, this will only work with friends who are marked as joinable and have valid join info
// I will set the "join failed" flag on my join info if this actually fails.
//
bool multi_game_friend_join( multi_game_friend *pfriend )
{
	return false;
}

#endif // defined(__LIVE_ENABLED) || defined(__PS3) || defined(__NX__)
