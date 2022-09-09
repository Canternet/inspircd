/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2017-2021 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2014-2016, 2018 Attila Molnar <attilamolnar@hush.com>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "inspircd.h"
#include "clientprotocolmsg.h"

#include "core_user.h"

enum
{
	// From RFC 1459.
	ERR_NOORIGIN = 409,
};

class CommandPass final
	: public SplitCommand
{
public:
	CommandPass(Module* parent)
		: SplitCommand(parent, "PASS", 1, 1)
	{
		penalty = 0;
		syntax = { "<password>" };
		works_before_reg = true;
	}

	CmdResult HandleLocal(LocalUser* user, const Params& parameters) override
	{
		// Check to make sure they haven't registered -- Fix by FCS
		if (user->registered == REG_ALL)
		{
			user->CommandFloodPenalty += 1000;
			user->WriteNumeric(ERR_ALREADYREGISTERED, "You may not reregister");
			return CmdResult::FAILURE;
		}
		user->password = parameters[0];

		return CmdResult::SUCCESS;
	}
};

class CommandPing final
	: public SplitCommand
{
public:
	CommandPing(Module* parent)
		: SplitCommand(parent, "PING", 1)
	{
		syntax = { "<cookie> [<servername>]" };
	}

	CmdResult HandleLocal(LocalUser* user, const Params& parameters) override
	{
		size_t origin = parameters.size() > 1 ? 1 : 0;
		if (parameters[origin].empty())
		{
			user->WriteNumeric(ERR_NOORIGIN, "No origin specified");
			return CmdResult::FAILURE;
		}

		ClientProtocol::Messages::Pong pong(parameters[0], origin ? parameters[1] : ServerInstance->Config->GetServerName());
		user->Send(ServerInstance->GetRFCEvents().pong, pong);
		return CmdResult::SUCCESS;
	}
};

class CommandPong final
	: public Command
{
public:
	CommandPong(Module* parent)
		: Command(parent, "PONG", 1)
	{
		penalty = 0;
		syntax = { "<cookie> [<servername>]" };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		size_t origin = parameters.size() > 1 ? 1 : 0;
		if (parameters[origin].empty())
		{
			user->WriteNumeric(ERR_NOORIGIN, "No origin specified");
			return CmdResult::FAILURE;
		}

		// set the user as alive so they survive to next ping
		LocalUser* localuser = IS_LOCAL(user);
		if (localuser)
		{
			// Increase penalty unless we've sent a PING and this is the reply
			if (localuser->lastping)
				localuser->CommandFloodPenalty += 1000;
			else
				localuser->lastping = 1;
		}
		return CmdResult::SUCCESS;
	}
};

void MessageWrapper::Wrap(const std::string& message, std::string& out)
{
	// If there is a fixed message, it is stored in prefix. Otherwise prefix contains
	// only the prefix, so append the message and the suffix
	out.assign(prefix);
	if (!fixed)
		out.append(message).append(suffix);
}

void MessageWrapper::ReadConfig(const char* prefixname, const char* suffixname, const char* fixedname)
{
	auto tag = ServerInstance->Config->ConfValue("options");
	fixed = tag->readString(fixedname, prefix);
	if (!fixed)
	{
		prefix = tag->getString(prefixname);
		suffix = tag->getString(suffixname);
	}
}

class CoreModUser final
	: public Module
{
	CommandAway cmdaway;
	CommandNick cmdnick;
	CommandPart cmdpart;
	CommandPass cmdpass;
	CommandPing cmdping;
	CommandPong cmdpong;
	CommandQuit cmdquit;
	CommandUser cmduser;
	CommandIson cmdison;
	CommandUserhost cmduserhost;
	SimpleUserMode invisiblemode;

public:
	CoreModUser()
		: Module(VF_CORE | VF_VENDOR, "Provides the AWAY, ISON, NICK, PART, PASS, PING, PONG, QUIT, USERHOST, and USER commands")
		, cmdaway(this)
		, cmdnick(this)
		, cmdpart(this)
		, cmdpass(this)
		, cmdping(this)
		, cmdpong(this)
		, cmdquit(this)
		, cmduser(this)
		, cmdison(this)
		, cmduserhost(this)
		, invisiblemode(this, "invisible", 'i')
	{
	}

	void ReadConfig(ConfigStatus& status) override
	{
		cmdpart.msgwrap.ReadConfig("prefixpart", "suffixpart", "fixedpart");
		cmdquit.msgwrap.ReadConfig("prefixquit", "suffixquit", "fixedquit");
	}
};

MODULE_INIT(CoreModUser)
