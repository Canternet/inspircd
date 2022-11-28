/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2018-2019 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2018 linuxdaemon <linuxdaemon.irc@gmail.com>
 *   Copyright (C) 2014 Attila Molnar <attilamolnar@hush.com>
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

class CoreModOper final
	: public Module
{
private:
	CommandDie cmddie;
	CommandKill cmdkill;
	CommandOper cmdoper;
	CommandRehash cmdrehash;
	CommandRestart cmdrestart;
	ModeUserOperator operatormode;
	ModeUserServerNoticeMask snomaskmode;

public:
	CoreModOper()
		: Module(VF_CORE | VF_VENDOR, "Provides the DIE, KILL, OPER, REHASH, and RESTART commands")
		, cmddie(this)
		, cmdkill(this)
		, cmdoper(this)
		, cmdrehash(this)
		, cmdrestart(this)
		, operatormode(this)
		, snomaskmode(this)
	{
	}

	void ReadConfig(ConfigStatus& status) override
	{
		auto security = ServerInstance->Config->ConfValue("security");
		cmdkill.hidenick = security->getString("hidekills");
		cmdkill.hideservicekills = security->getBool("hideservicekills", security->getBool("hideulinekills"));
	}

	void OnPostOperLogin(User* user) override
	{
		LocalUser* luser = IS_LOCAL(user);
		if (!luser)
			return;

		luser->WriteNumeric(RPL_YOUAREOPER, InspIRCd::Format("You are now %s %s",
			user->oper->GetType()[0] ? "an" : "a", user->oper->GetType().c_str()));

		ServerInstance->SNO.WriteToSnoMask('o', "%s (%s) is now a server operator of type %s (using account %s)",
			user->nick.c_str(), user->MakeHost().c_str(), user->oper->GetType().c_str(),
			user->oper->GetName().c_str());

		const std::string vhost = luser->oper->GetConfig()->getString("vhost");
		if (!vhost.empty())
			user->ChangeDisplayedHost(vhost);

		const std::string klass = luser->oper->GetConfig()->getString("class");
		if (!klass.empty())
			luser->SetClass(klass);
	}
};

MODULE_INIT(CoreModOper)
