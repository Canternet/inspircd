/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013, 2018-2020 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2013 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2004, 2006 Craig Edwards <brain@inspircd.org>
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
#include "modules/extban.h"

enum
{
	// From UnrealIRCd.
	ERR_CANTJOINOPERSONLY = 520
};

class OperExtBan final
	: public ExtBan::MatchingBase
{
private:
	std::string space;
	std::string underscore;

public:
	OperExtBan(Module* Creator)
		: ExtBan::MatchingBase(Creator, "oper", 'O')
		, space(" ")
		, underscore("_")
	{
	}

	bool IsMatch(User* user, Channel* channel, const std::string& text) override
	{
		// If the user is not an oper they can't match this.
		if (!user->IsOper())
			return false;

		// Replace spaces with underscores as they're prohibited in mode parameters.
		std::string opername(user->oper->GetType());
		stdalgo::string::replace_all(opername, space, underscore);
		return InspIRCd::Match(opername, text);
	}
};

class ModuleOperChans final
	: public Module
{
private:
	SimpleChannelMode oc;
	OperExtBan extban;

public:
	ModuleOperChans()
		: Module(VF_VENDOR, "Adds channel mode O (operonly) which prevents non-server operators from joining the channel.")
		, oc(this, "operonly", 'O', true)
		, extban(this)
	{
	}

	ModResult OnUserPreJoin(LocalUser* user, Channel* chan, const std::string& cname, std::string& privs, const std::string& keygiven, bool override) override
	{
		if (!override && chan && chan->IsModeSet(oc) && !user->IsOper())
		{
			user->WriteNumeric(ERR_CANTJOINOPERSONLY, chan->name, InspIRCd::Format("Only server operators may join %s (+O is set)", chan->name.c_str()));
			return MOD_RES_DENY;
		}
		return MOD_RES_PASSTHRU;
	}
};

MODULE_INIT(ModuleOperChans)
