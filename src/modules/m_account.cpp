/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2019 linuxdaemon <linuxdaemon.irc@gmail.com>
 *   Copyright (C) 2013, 2017-2022 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2015 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2012 Shawn Smith <ShawnSmith0828@gmail.com>
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2006, 2008 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2006, 2008 Craig Edwards <brain@inspircd.org>
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
#include "modules/account.h"
#include "modules/callerid.h"
#include "modules/ctctags.h"
#include "modules/extban.h"
#include "modules/exemption.h"
#include "modules/who.h"
#include "modules/whois.h"

enum
{
	// From ircd-hybrid?
	ERR_NEEDREGGEDNICK = 477,

	// From IRCv3 sasl-3.1.
	RPL_LOGGEDIN = 900,
	RPL_LOGGEDOUT = 901
};

class AccountExtItemImpl final
	: public StringExtItem
{
	Events::ModuleEventProvider eventprov;

public:
	AccountExtItemImpl(Module* mod)
		: StringExtItem(mod, "accountname", ExtensionType::USER, true)
		, eventprov(mod, "event/account")
	{
	}

	void FromNetwork(Extensible* container, const std::string& value) noexcept override
	{
		if (container->extype != this->extype)
			return;

		StringExtItem::FromNetwork(container, value);

		User* user = static_cast<User*>(container);
		if (IS_LOCAL(user))
		{
			if (value.empty())
			{
				// Logged out.
				user->WriteNumeric(RPL_LOGGEDOUT, user->GetFullHost(), "You are now logged out");
			}
			else
			{
				// Logged in.
				user->WriteNumeric(RPL_LOGGEDIN, user->GetFullHost(), value, InspIRCd::Format("You are now logged in as %s", value.c_str()));
			}
		}

		eventprov.Call(&Account::EventListener::OnAccountChange, user, value);
	}
};


class AccountNicksExtItem final
	: public SimpleExtItem<Account::NickList>
{
public:
	AccountNicksExtItem(Module* mod)
		: SimpleExtItem<Account::NickList>(mod, "accountnicks", ExtensionType::USER, true)
	{
	}

	void FromInternal(Extensible* container, const std::string& value) noexcept override
	{
		if (container->extype != this->extype)
			return;

		auto list = new Account::NickList();
		irc::spacesepstream nickstream(value);
		for (std::string nick; nickstream.GetToken(nick); )
			list->insert(nick);

		if (list->empty())
		{
			// The remote sent an empty list of nicknames for some reason.
			delete list;
			Unset(container);
		}
		else
		{
			// The remote sent a non-zero list of nicks; set it.
			Set(container, list);
		}
	}

	std::string ToInternal(const Extensible* container, void* item) const noexcept override
	{
		auto list = static_cast<Account::NickList*>(item);
		return stdalgo::string::join(*list);
	}
};


class AccountAPIImpl final
	: public Account::APIBase
{
private:
	AccountExtItemImpl accountext;
	StringExtItem accountidext;
	AccountNicksExtItem accountnicksext;
	UserModeReference identifiedmode;

public:
	AccountAPIImpl(Module* mod)
		: Account::APIBase(mod)
		, accountext(mod)
		, accountidext(mod, "accountid", ExtensionType::USER, true)
		, accountnicksext(mod)
		, identifiedmode(mod, "u_registered")
	{
	}

	std::string* GetAccountId(const User* user) const override
	{
		return accountidext.Get(user);
	}

	std::string* GetAccountName(const User* user) const override
	{
		return accountext.Get(user);
	}

	Account::NickList* GetAccountNicks(const User* user) const override
	{
		return accountnicksext.Get(user);
	}

	bool IsIdentifiedToNick(const User* user) override
	{
		if (user->IsModeSet(identifiedmode))
			return true; // User has +r set.

		// Check whether their current nick is in their nick list.
		Account::NickList* nicks = accountnicksext.Get(user);
		return nicks && nicks->find(user->nick) != nicks->end();
	}
};

class AccountExtBan final
	: public ExtBan::MatchingBase
{
private:
	AccountAPIImpl& accountapi;

public:
	AccountExtBan(Module* Creator, AccountAPIImpl& AccountAPI)
		: ExtBan::MatchingBase(Creator, "account", 'R')
		, accountapi(AccountAPI)
	{
	}

	bool IsMatch(User* user, Channel* channel, const std::string& text) override
	{
		const std::string* account = accountapi.GetAccountName(user);
		return account && InspIRCd::Match(*account, text);
	}
};

class UnauthedExtBan final
	: public ExtBan::MatchingBase
{
private:
	AccountAPIImpl& accountapi;

public:
	UnauthedExtBan(Module* Creator, AccountAPIImpl& AccountAPI)
		: ExtBan::MatchingBase(Creator, "unauthed", 'U')
		, accountapi(AccountAPI)
	{
	}

	bool IsMatch(User* user, Channel* channel, const std::string& text) override
	{
		const std::string* account = accountapi.GetAccountName(user);
		return !account && channel->CheckBan(user, text);
	}
};

class ModuleAccount final
	: public Module
	, public CTCTags::EventListener
	, public Who::EventListener
	, public Whois::EventListener
{
private:
	CallerID::API calleridapi;
	CheckExemption::EventProvider exemptionprov;
	SimpleChannelMode reginvitemode;
	SimpleChannelMode regmoderatedmode;
	SimpleUserMode regdeafmode;
	AccountAPIImpl accountapi;
	AccountExtBan accountextban;
	UnauthedExtBan unauthedextban;

public:
	ModuleAccount()
		: Module(VF_VENDOR | VF_OPTCOMMON, "Adds various channel and user modes relating to accounts.")
		, CTCTags::EventListener(this)
		, Who::EventListener(this)
		, Whois::EventListener(this)
		, calleridapi(this)
		, exemptionprov(this)
		, reginvitemode(this, "reginvite", 'R')
		, regmoderatedmode(this, "regmoderated", 'M')
		, regdeafmode(this, "regdeaf", 'R')
		, accountapi(this)
		, accountextban(this, accountapi)
		, unauthedextban(this, accountapi)
	{
	}

	ModResult OnWhoLine(const Who::Request& request, LocalUser* source, User* user, Membership* memb, Numeric::Numeric& numeric) override
	{
		size_t flag_index;
		if (!request.GetFieldIndex('f', flag_index))
			return MOD_RES_PASSTHRU;

		if (accountapi.IsIdentifiedToNick(user))
			numeric.GetParams()[flag_index].push_back('r');

		return MOD_RES_PASSTHRU;
	}

	void OnWhois(Whois::Context& whois) override
	{
		const std::string* account = accountapi.GetAccountName(whois.GetTarget());
		if (account)
			whois.SendLine(RPL_WHOISACCOUNT, *account, "is logged in as");

		if (accountapi.IsIdentifiedToNick(whois.GetTarget()))
			whois.SendLine(RPL_WHOISREGNICK, "is a registered nick");
	}

	ModResult HandleMessage(User* user, const MessageTarget& target)
	{
		if (!IS_LOCAL(user))
			return MOD_RES_PASSTHRU;


		const std::string* account = accountapi.GetAccountName(user);
		switch (target.type)
		{
			case MessageTarget::TYPE_CHANNEL:
			{
				Channel* targetchan = target.Get<Channel>();

				if (!targetchan->IsModeSet(regmoderatedmode) || account)
					return MOD_RES_PASSTHRU;

				if (CheckExemption::Call(exemptionprov, user, targetchan, "regmoderated") == MOD_RES_ALLOW)
					return MOD_RES_PASSTHRU;

				// User is messaging a +M channel and is not registered or exempt.
				user->WriteNumeric(ERR_NEEDREGGEDNICK, targetchan->name, "You need to be identified to a registered account to message this channel");
				return MOD_RES_DENY;
			}
			case MessageTarget::TYPE_USER:
			{
				User* targetuser = target.Get<User>();
				if (!targetuser->IsModeSet(regdeafmode)  || account)
					return MOD_RES_PASSTHRU;

				if (calleridapi && calleridapi->IsOnAcceptList(user, targetuser))
					return MOD_RES_PASSTHRU;

				// User is messaging a +R user and is not registered or on an accept list.
				user->WriteNumeric(ERR_NEEDREGGEDNICK, targetuser->nick, "You need to be identified to a registered account to message this user");
				return MOD_RES_DENY;
			}
			case MessageTarget::TYPE_SERVER:
				break;
		}
		return MOD_RES_PASSTHRU;
	}

	ModResult OnUserPreMessage(User* user, const MessageTarget& target, MessageDetails& details) override
	{
		return HandleMessage(user, target);
	}

	ModResult OnUserPreTagMessage(User* user, const MessageTarget& target, CTCTags::TagMessageDetails& details) override
	{
		return HandleMessage(user, target);
	}

	ModResult OnUserPreJoin(LocalUser* user, Channel* chan, const std::string& cname, std::string& privs, const std::string& keygiven, bool override) override
	{
		if (override)
			return MOD_RES_PASSTHRU;


		const std::string* account = accountapi.GetAccountName(user);
		if (chan)
		{
			if (chan->IsModeSet(reginvitemode))
			{
				if (!account)
				{
					// joining a +R channel and not identified
					user->WriteNumeric(ERR_NEEDREGGEDNICK, chan->name, "You need to be identified to a registered account to join this channel");
					return MOD_RES_DENY;
				}
			}
		}
		return MOD_RES_PASSTHRU;
	}

	ModResult OnPreOperLogin(LocalUser* user, const std::shared_ptr<OperAccount>& oper, bool automatic) override
	{
		const std::string accountstr = oper->GetConfig()->getString("account");
		if (accountstr.empty())
			return MOD_RES_PASSTHRU;

		const std::string* accountid = accountapi.GetAccountId(user);
		const std::string* accountname = accountapi.GetAccountName(user);

		irc::spacesepstream accountstream(accountstr);
		for (std::string account; accountstream.GetToken(account); )
		{
			if (accountid && irc::equals(account, *accountid))
				return MOD_RES_PASSTHRU; // Matches on account id.

			if (accountname && irc::equals(account, *accountname))
				return MOD_RES_PASSTHRU; // Matches on account name.
		}

		if (!automatic)
		{
			ServerInstance->SNO.WriteGlobalSno('o', "%s (%s) [%s] failed to log into the \x02%s\x02 oper account because they are not logged into the correct user account.",
				user->nick.c_str(), user->MakeHost().c_str(), user->GetIPString().c_str(), oper->GetName().c_str());
		}
		return MOD_RES_DENY; // Account required but it does not match.
	}

	ModResult OnSetConnectClass(LocalUser* user, const ConnectClass::Ptr& myclass) override
	{
		const char* error = nullptr;
		if (stdalgo::string::equalsci(myclass->config->getString("requireaccount"), "nick"))
		{
			if (!accountapi.GetAccountName(user) && !accountapi.IsIdentifiedToNick(user))
				error = "an account matching their current nickname";
		}
		else if (myclass->config->getBool("requireaccount"))
		{
			if (!accountapi.GetAccountName(user))
				error = "an account";
		}

		if (error)
		{
			ServerInstance->Logs.Debug("CONNECTCLASS", "The %s connect class is not suitable as it requires the user to be logged into %s",
				myclass->GetName().c_str(), error);
			return MOD_RES_DENY;
		}
		return MOD_RES_PASSTHRU;
	}
};

MODULE_INIT(ModuleAccount)
