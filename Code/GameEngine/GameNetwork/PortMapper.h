/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PortMapper.h -- asks the home router to forward the game's UDP ports to this computer.
//
// Players behind a router can then be reached from the internet without forwarding the ports by
// hand. A worker thread tries NAT-PMP at the default gateway first, then UPnP (SSDP search, device
// description, SOAP); it renews the mappings at half their lifetime and removes them on stop().
// With an IPv6 address given it also asks an IGD version 2 to open its IPv6 firewall for the same
// ports (pinholes, WANIPv6FirewallControl): IPv6 needs no forwarding, but routers keep incoming
// connections out by default.
// External port = internal port: the other players address every player at the game's fixed ports.
// Protocol messages: Engine/PortMapping.h.

#pragma once

#ifndef _PORTMAPPER_H_
#define _PORTMAPPER_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Engine/FormatCheck.h"
#include "Engine/PortMapping.h"

class PortMapper
{
public:
	enum State
	{
		STATE_OFF,
		STATE_WORKING,
		STATE_MAPPED,
		STATE_FAILED,	///< no router answered or it refused; the ports stay as they are
	};

	/// Why STATE_FAILED: no router answered at all, or one answered and refused.
	enum Failure
	{
		FAILURE_NONE,
		FAILURE_NO_ROUTER,
		FAILURE_REFUSED,
	};

	enum PinholeState
	{
		PINHOLE_OFF,
		PINHOLE_WORKING,
		PINHOLE_OPEN,
		PINHOLE_NOT_NEEDED,		///< the router's IPv6 firewall is off
		PINHOLE_NO_SERVICE,		///< no router offers IPv6 pinholes over UPnP
		PINHOLE_REFUSED,		///< see pinholeRefusalCode()
	};

	/// How the UPnP search went: answered; silent while the gateway answers directly (the answers
	/// were dropped on this computer, most likely by a firewall); silent with no answer either way
	/// (UPnP probably off in the router).
	enum SearchOutcome
	{
		SEARCH_NOT_RUN,
		SEARCH_ANSWERED,
		SEARCH_FILTERED,
		SEARCH_SILENT,
	};

	enum Method
	{
		METHOD_NONE,
		METHOD_NATPMP,
		METHOD_UPNP,
	};

	static constexpr int MAX_PORTS = 4;

	struct Settings
	{
		std::uint32_t localIP = 0;			///< the address the game binds, host byte order
		std::uint16_t ports[MAX_PORTS] = {};
		int portCount = 0;
		std::uint32_t gateway = 0;			///< NAT-PMP target; 0: the default gateway towards the internet
		std::uint16_t natpmpPort = 5351;
		std::uint32_t ssdpTarget = 0xEFFFFFFAu;	///< 239.255.255.250; tests give a unicast address
		std::uint16_t ssdpPort = 1900;
		std::uint32_t lifetimeSeconds = 3600;
		std::uint32_t timeoutMs = 2000;		///< per step (NAT-PMP answer, SSDP search, HTTP request)
		bool tryNatPmp = true;
		bool tryUpnp = true;
		std::string localIPv6;				///< global IPv6 address (text) to open pinholes for; empty: none
		/// Port and path of descriptions to ask the gateway for when the search stays silent; empty:
		/// where common routers keep theirs (tests give their own).
		std::vector<std::pair<std::uint16_t, std::string>> directDescriptions;
		bool describeSecuritySoftware = true;	///< on failure, name the firewalls of this computer in the log
	};

	PortMapper() = default;
	~PortMapper();
	PortMapper(const PortMapper &) = delete;
	PortMapper &operator=(const PortMapper &) = delete;

	/// Starts the worker unless it runs already.
	void start(const Settings &settings);
	/// Removes the mappings (a few seconds at most) and ends the worker.
	void stop();

	State state() const { return m_state.load(); }
	Method method() const { return m_method.load(); }
	std::uint32_t externalIP() const { return m_externalIP.load(); }	///< host byte order, 0 if unknown
	Failure failure() const { return m_failure.load(); }
	/// With FAILURE_REFUSED: the UPnP error code or NAT-PMP result of the last refusal, 0 if none came.
	int refusalCode() const { return m_refusalCode.load(); }
	PinholeState pinholeState() const { return m_pinholeState.load(); }
	SearchOutcome searchOutcome() const { return m_searchOutcome.load(); }
	/// What the worker did and what the routers answered, one line per step (gateway, search answers
	/// with the router's name, services, error codes). Only the router knows why it refuses; the
	/// lobby writes these lines to a file the player can pass on.
	std::vector<std::string> log() const;
	/// With PINHOLE_REFUSED: the UPnP error code, 0 if none came.
	int pinholeRefusalCode() const { return m_pinholeRefusal.load(); }
	/// Grows with every change of state; callers poll it to report a change once.
	unsigned changeCount() const { return m_changes.load(); }

private:
	void run();
	bool mapNatPmp(std::uint32_t lifetime);
	bool discoverUpnp();
	bool askGatewayDirectly();
	bool mapUpnp(std::uint32_t lifetime);
	bool mapUpnpService(std::uint32_t lifetime);
	void refused(int code);
	bool openPinholes(std::uint32_t lifetime);
	bool renewPinholes(std::uint32_t lifetime);
	void removePinholes(std::uint32_t timeoutMs);
	void setPinholeState(PinholeState state, int refusal = 0);
	void note(const char *format, ...) ENGINE_PRINTF_FORMAT(2, 3);
	bool soapCall(const std::string &controlUrl, const std::string &serviceType, const std::string &action,
		const std::string &body, std::uint32_t timeoutMs, const std::atomic<bool> &stop, ::Engine::PortMapping::SoapResult &result);
	bool renew();
	void removeMappings();
	bool upnpRequest(const std::string &action, const std::string &body, std::string &response);

	/// A gateway that answered the search: its description, fetched once per start().
	struct UpnpDevice
	{
		std::string location;
		std::uint32_t host = 0;
		std::string description;
	};
	bool waitStop(std::uint32_t ms);
	bool stopping() const { return m_stop.load(); }
	void setState(State state);

	Settings m_settings;
	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::atomic<bool> m_stop{false};
	std::atomic<State> m_state{STATE_OFF};
	std::atomic<Method> m_method{METHOD_NONE};
	std::atomic<std::uint32_t> m_externalIP{0};
	std::atomic<Failure> m_failure{FAILURE_NONE};
	std::atomic<int> m_refusalCode{0};
	std::atomic<PinholeState> m_pinholeState{PINHOLE_OFF};
	std::atomic<int> m_pinholeRefusal{0};
	std::atomic<SearchOutcome> m_searchOutcome{SEARCH_NOT_RUN};
	mutable std::mutex m_logMutex;
	std::vector<std::string> m_log;
	std::atomic<unsigned> m_changes{0};

	// worker only
	std::uint32_t m_gateway = 0;
	std::string m_upnpServiceType;
	std::string m_upnpControlUrl;
	bool m_upnpPermanent = false;	///< the router accepts only leases without end
	bool m_routerAnswered = false;
	bool m_upnpDiscovered = false;
	std::vector<UpnpDevice> m_upnpDevices;
	std::string m_firewallServiceType;
	std::string m_firewallControlUrl;
	int m_pinholeIds[MAX_PORTS] = {};
	int m_pinholeCount = 0;
};

/// The one instance of the program.
PortMapper &ThePortMapper();
/// Stops the mapper and frees it (end of the program).
void ReleasePortMapper();

#endif // _PORTMAPPER_H_
