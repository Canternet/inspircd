/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2020 Matt Schatz <genius3000@g3k.solutions>
 *   Copyright (C) 2013-2014, 2017-2018 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012, 2014, 2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2006, 2008, 2010 Craig Edwards <brain@inspircd.org>
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
#include "core_oper.h"

CommandOper::CommandOper(Module* parent)
	: SplitCommand(parent, "OPER", 2, 2)
{
	syntax = { "<username> <password>" };
}

CmdResult CommandOper::HandleLocal(LocalUser* user, const Params& parameters)
{
	bool match_user = false;
	bool match_pass = false;

	auto i = ServerInstance->Config->OperAccounts.find(parameters[0]);
	if (i != ServerInstance->Config->OperAccounts.end())
	{
		std::shared_ptr<OperAccount> ifo = i->second;
		std::shared_ptr<ConfigTag> tag = ifo->GetConfig();
		match_user = true;
		match_pass = ServerInstance->PassCompare(user, tag->getString("password"), parameters[1], tag->getString("hash"));

		if (match_pass)
		{
			user->OperLogin(ifo);
			return CmdResult::SUCCESS;
		}
	}

	std::string fields;
	if (!match_user)
		fields.append("username ");
	if (!match_pass)
		fields.append("password ");
	fields.erase(fields.length() - 1, 1);

	// Tell them they failed (generically) and lag them up to help prevent brute-force attacks
	user->WriteNumeric(ERR_NOOPERHOST, "Invalid oper credentials");
	user->CommandFloodPenalty += 10000;

	ServerInstance->SNO.WriteGlobalSno('o', "WARNING! Failed oper attempt by %s using login '%s': The following fields do not match: %s", user->GetFullRealHost().c_str(), parameters[0].c_str(), fields.c_str());
	return CmdResult::FAILURE;
}
