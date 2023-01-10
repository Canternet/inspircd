/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2013-2014, 2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2013, 2017-2018, 2022 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2013 Daniel Vassdal <shutter@canternet.org>
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
#include "listmode.h"
#include "numerichelper.h"

class CommandRMode final
	: public Command
{
public:
	CommandRMode(Module* Creator)
		: Command(Creator, "RMODE", 2, 3)
	{
		syntax = { "<channel> <mode> [<pattern>]" };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		auto* chan = ServerInstance->Channels.Find(parameters[0]);
		if (!chan)
		{
			user->WriteNumeric(Numerics::NoSuchChannel(parameters[0]));
			return CmdResult::FAILURE;
		}

		unsigned char modeletter = parameters[1][0];
		ModeHandler* mh = ServerInstance->Modes.FindMode(modeletter, MODETYPE_CHANNEL);
		if (!mh || parameters[1].size() > 1)
		{
			user->WriteNumeric(ERR_UNKNOWNMODE, parameters[0], "is not a recognised channel mode.");
			return CmdResult::FAILURE;
		}

		if (chan->GetPrefixValue(user) < mh->GetLevelRequired(false))
		{
			user->WriteNumeric(Numerics::ChannelPrivilegesNeeded(chan, mh->GetLevelRequired(false), InspIRCd::Format("unset channel mode %c (%s)",
				mh->GetModeChar(), mh->name.c_str())));
			return CmdResult::FAILURE;
		}

		std::string pattern = parameters.size() > 2 ? parameters[2] : "*";
		PrefixMode* pm;
		ListModeBase* lm;
		ListModeBase::ModeList* ml;
		Modes::ChangeList changelist;

		if ((pm = mh->IsPrefixMode()))
		{
			// As user prefix modes don't have a GetList() method, let's iterate through the channel's users.
			for (const auto& [u, memb] : chan->GetUsers())
			{
				if (!InspIRCd::Match(u->nick, pattern))
					continue;

				if (memb->HasMode(pm) && !((u == user) && (pm->GetPrefixRank() > VOICE_VALUE)))
					changelist.push_remove(mh, u->nick);
			}
		}
		else if ((lm = mh->IsListModeBase()) && ((ml = lm->GetList(chan)) != nullptr))
		{
			for (const auto& entry : *ml)
			{
				if (InspIRCd::Match(entry.mask, pattern))
					changelist.push_remove(mh, entry.mask);
			}
		}
		else
		{
			if (chan->IsModeSet(mh))
				changelist.push_remove(mh);
		}

		ServerInstance->Modes.Process(user, chan, nullptr, changelist);
		return CmdResult::SUCCESS;
	}
};

class ModuleRMode final
	: public Module
{
private:
	CommandRMode cmd;

public:
	ModuleRMode()
		: Module(VF_VENDOR, "Allows removal of channel list modes using glob patterns.")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleRMode)
