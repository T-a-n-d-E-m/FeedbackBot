// This is free and unencumbered software released into the public domain.
// 
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
// 
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
// 
// For more information, please refer to <http://unlicense.org/>

#include <stdio.h>
#include <unistd.h>

#include <dpp/dpp.h>

#include "../BadgeBot/log.h" // Not versioned here: From a private repo

// The Discord bot token is stored here. This is private information so isn't stored in this repository.
static const size_t DISCORD_TOKEN_LENGTH_MAX = 72;
static char g_discord_token[DISCORD_TOKEN_LENGTH_MAX + 1];

// The two XDHS servers. The private one is used for bot development.
static const std::uint64_t GUILD_XDHS_PUBLIC_ID  = 528728694680715324; // The public XDHS server
static const std::uint64_t GUILD_XDHS_PRIVATE_ID = 882164794566791179; // Our private bot testing server

// The channel to send the anonymous messages to.
static const std::uint64_t MESSAGE_CHANNEL_PUBLIC_ID = 1082557504887730268; // #feedback
static const std::uint64_t MESSAGE_CHANNEL_PRIVATE_ID = 1082090578067607632; // #feedback

// This signal handling is extremely basic but it's all we need for such a simple bot.
static bool g_quit = false;
static int g_exit_code = 0;

static void sig_handler(int signo) {
	switch(signo) {
		case SIGINT:  // Fall through
		case SIGABRT: // Fall through
		case SIGHUP:  // Fall through
		case SIGTERM:
			log(LOG_LEVEL_INFO, strsignal(signo));
			g_quit = true;
			break;

		default: log(LOG_LEVEL_INFO, "Caught unhandled signal: %d", signo);
	}

	g_exit_code = signo;
}


int main() {
	{   // Read the Discord token from the discord.token file
		FILE* f = fopen("discord.token", "rb");
		if(f != NULL) {
			memset(g_discord_token, 0, DISCORD_TOKEN_LENGTH_MAX + 1);
			(void)fread(g_discord_token, 1, DISCORD_TOKEN_LENGTH_MAX, f);
			fclose(f);
		} else {
			log(LOG_LEVEL_ERROR, "Failed to read discord.token file.");
			return EXIT_FAILURE;
		}
	}

	// FeedbackBot runs as a Linux systemd service, so we need to gracefully handle these signals.
	(void)signal(SIGINT, sig_handler);
	(void)signal(SIGABRT, sig_handler);
	(void)signal(SIGHUP, sig_handler);
	(void)signal(SIGTERM, sig_handler);
	(void)signal(SIGKILL, sig_handler);

	// Set up logging to an external file. This only logs bot events, not the messages the bot sends.
	log_init("feedbackbot.log");

	log(LOG_LEVEL_INFO, "====== Feedback Bot starting ======");
	log(LOG_LEVEL_INFO, "Commit version: %s", GIT_COMMIT_HASH);
	log(LOG_LEVEL_INFO, "libDPP++ version: %s", dpp::utility::version().c_str());

	// Create the bot and connect to Discord.
	dpp::cluster bot(g_discord_token, dpp::i_all_intents);

	// Override the default DPP logger with ours.
	bot.on_log([](const dpp::log_t& event) {
		LOG_LEVEL level = g_log_level;
		switch(event.severity) {
			case dpp::ll_trace:    level = LOG_LEVEL_DEBUG;   break;
			case dpp::ll_debug:    level = LOG_LEVEL_DEBUG;   break;
			case dpp::ll_info:     level = LOG_LEVEL_INFO;    break;
			case dpp::ll_warning:  level = LOG_LEVEL_WARNING; break;
			case dpp::ll_error:    level = LOG_LEVEL_ERROR;   break;
			case dpp::ll_critical: level = LOG_LEVEL_ERROR;   break;
		}
		log(level, "%s", event.message.c_str());
	});

	// Called when Discord has connected the bot to a guild.
	bot.on_guild_create([&bot](const dpp::guild_create_t& event) {
		const std::uint64_t guild_id = (std::uint64_t) event.created->id;
		log(LOG_LEVEL_INFO, "on_guild_create: Guild name:[%s] Guild ID:[%lu]", event.created->name.c_str(), guild_id);

		// Check the guild connecting is actually an XDHS guild.
		if((guild_id != GUILD_XDHS_PUBLIC_ID) && (guild_id != GUILD_XDHS_PRIVATE_ID)) return;

		// This bot only has a single command, defined here:
		dpp::slashcommand cmd("feedback", "Send an anonymous feedback message to the XDHS team.", bot.me.id);
		cmd.default_member_permissions = dpp::p_use_application_commands ;
		cmd.add_option(dpp::command_option(dpp::co_string, "text", "The message text to send. Max 1800 characters.", true));
		cmd.add_option(dpp::command_option(dpp::co_attachment, "attachment", "Add an attachment (screenshot, log file, etc.) to the message.", false));

		// Add the command to this guild.
		bot.guild_command_create(cmd, event.created->id);
	});

	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {
		const auto command_name = event.command.get_command_name();
		const std::uint64_t guild_id = event.command.get_guild().id;

		if(command_name == "feedback") {
			// Get the message text and check it isn't too long.
			std::string text = std::get<std::string>(event.get_parameter("text"));
			if(text.length() > 1800) {
				dpp::message reply;
				reply.set_flags(dpp::m_ephemeral); // Only the member who used the command will see the reply.
				reply.set_content("Your message exceeds the maximum allowed 1800 characters.");
				event.reply(reply);
				return;
			}

			// Check if an attachment was sent too.
			std::string attachment_url;
			{
				auto opt = event.get_parameter("attachment");
				if(std::holds_alternative<dpp::snowflake>(opt)) {
					dpp::snowflake id = std::get<dpp::snowflake>(opt);
					auto itr = event.command.resolved.attachments.find(id);
					attachment_url = itr->second.url;
				}
			}

			// Construct a new message - No details about the member who called this command are used.
			dpp::message message;
			message.set_type(dpp::message_type::mt_default);
			message.set_guild_id(guild_id); // Send the message to the same guild the command came from.
			if(guild_id == GUILD_XDHS_PUBLIC_ID) { // Set the channel ID to the correct value for this guild.
				message.set_channel_id(MESSAGE_CHANNEL_PUBLIC_ID);
			} else {
				message.set_channel_id(MESSAGE_CHANNEL_PRIVATE_ID);
			}
			std::string full_text = "Anonymous message received:\n> " + text;
			if(attachment_url.length() > 0) {
				full_text += "\nAttachment: " + attachment_url;
			}
			message.set_content(full_text); // Add the members message.
			message.set_allowed_mentions(false, false, false, false, {}, {}); // Don't allow messages to ping team members.

			// Send the message to the feedback channel. Only XDHS team members can view this channel.
			bot.message_create(message);

			// Reply to the member who used the command.
			dpp::message reply;
			reply.set_flags(dpp::m_ephemeral); // An ephemeral message means only the member who used the command will see the reply.
			reply.set_content("Your message has been sent anonymously to the XDHS team.");
			event.reply(reply);
		}
	});

	bot.on_ready([&bot](const dpp::ready_t& event) {
		log(LOG_LEVEL_INFO, "on_ready received");
	});

	bot.start(true);

	bot.set_presence({dpp::presence_status::ps_online, dpp::activity_type::at_watching, GIT_COMMIT_HASH});

	// Spin until we get a signal to quit.
	while(!g_quit) sleep(1);

	return g_exit_code;
}
