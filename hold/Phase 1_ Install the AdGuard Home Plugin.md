Phase 1: Install the AdGuard Home Plugin
OPNsense doesn't include AdGuard Home by default; you need the community repository.

Enable SSH: Go to System > Settings > Administration. Check Enable Secure Shell and Permit root user login. Save.

Add the Repo: SSH into your OPNsense (using PuTTY or Terminal) and run: fetch -o /usr/local/etc/pkg/repos/mimugmail.conf https://www.routerperformance.net/mimugmail.conf

Install the Plugin: In the Web UI, go to System > Firmware > Plugins. Search for os-adguardhome-maxit and click the + to install.

Enable the Service: Go to Services > AdGuardHome > General. Check Enable and Primary DNS, then click Save.

Phase 2: Configure Unbound (The "Back-end")
We need Unbound to move off the standard port so AdGuard can take over the "front-end."

Change Port: Go to Services > Unbound DNS > General.

Listen Port: Change from 53 to 5353.

Enable Local Registration: Ensure Register DHCP Leases and Register DHCP Static Mappings are checked. This allows you to see device names (like "Work-Laptop") in AdGuard instead of just IP addresses.

Save & Apply Changes.

Phase 3: Setup the AdGuard Web Interface
Access the Wizard: Open a new tab and go to http://192.168.1.1:3000 (replace with your OPNsense IP).

Admin Interface: Set this to your LAN IP on port 8080. (Do not use 80, as OPNsense uses that).

DNS Server: Set this to All Interfaces on port 53.

Finish the Wizard: Create your username and password.

Phase 4: Link AdGuard to Unbound
Now we tell AdGuard to send its "clean" requests to Unbound.

Upstream DNS: In the AdGuard Web UI, go to Settings > DNS Settings.

Upstream Servers: Delete everything and enter: 127.0.0.1:5353

Private Reverse DNS: Scroll down and enter 127.0.0.1:5353 here as well. This ensures AdGuard asks Unbound for local device names.

Test & Save: Click Test Upstreams. If it says "OK," click Apply.

Phase 5: DHCP & System Settings
Finally, make sure OPNsense and your devices actually use this new setup.

System DNS: Go to System > Settings > General.

DNS Servers: Clear all entries (or set only 127.0.0.1).

DNS Server Options: Uncheck Allow DNS server list to be overridden by WAN.

DHCP Server: Go to Services > Dnsmasq DNS & DHCP > General (or your active DHCP service).

Ensure the DNS Server field is either Empty (so it defaults to the OPNsense IP) or explicitly set to your OPNsense LAN IP.

Apply All: Reboot your computer or "Renew Lease" on your devices to pick up the new settings.

How to verify it's working:
AdGuard Dashboard: You should see "Top Queried Domains" filling up with data.

Local DNS: Try pinging your OPNsense hostname (e.g., ping opnsense.local). If it resolves, Unbound and AdGuard are talking correctly.

Phase 1: Preparation (The Community Repo)OPNsense doesn't include AdGuard Home in the standard list, so we add the trusted Mimugmail repository.Open OPNsense Web UI.Go to System > Settings > Administration. Ensure Enable Secure Shell is checked.Open a terminal (or Putty) and SSH into your router: ssh root@192.168.1.1.Run this command to add the repo:fetch -o /usr/local/etc/pkg/repos/mimugmail.conf https://www.routerperformance.net/mimugmail.confBack in the Web UI, go to System > Firmware > Plugins. Click Check for updates, then search for os-adguardhome-maxit and click the + to install.Phase 2: Move Unbound to a Custom PortSince AdGuard Home needs to occupy the standard DNS port (53), we must move Unbound "out of the way."Navigate to Services > Unbound DNS > General.Listen Port: Change this from 53 to 5353.Network Interfaces: Select Loopback (highly recommended) or All.Local Registration: Ensure Register DHCP Leases and Register DHCP Static Mappings are checked. This is how AdGuard will know your devices' names.Click Save and Apply Changes.Phase 3: The AdGuard Home "First Run" WizardNavigate to Services > AdGuardHome > General. Check Enable and click Save.Click the link in the UI or go to http://192.168.1.1:3000.Admin Interface: Set to LAN (e.g., 192.168.1.1) and Port 8080 (OPNsense uses 80/443).DNS Server: Set to All Interfaces and Port 53.Complete the wizard by creating your login credentials.Phase 4: Connecting the Two (Crucial Step)Now we tell AdGuard to use Unbound as its "brain."Log in to AdGuard Home (at 192.168.1.1:8080).Go to Settings > DNS Settings.Upstream DNS Servers: Delete everything and enter:127.0.0.1:5353Private reverse DNS servers: Enter:127.0.0.1:5353Check these boxes:[x] Use private reverse DNS resolvers[x] Enable reverse resolving of clients' IP addressesClick Apply.Phase 5: Optimization & High-Performance ListsTo make this truly "the best," you need the right blocklists. In AdGuard, go to Filters > DNS Blocklists and add these:List NameWhy you want itHaGeZi - Multi PROThe "Gold Standard" for 2026. High blocking, zero breakage.OISD (Big)Very popular "set and forget" list.AdGuard Base FilterEssential for general web ad-blocking.Dandelion Sprout's Game ConsoleIf you have an Xbox/PlayStation (blocks telemetry).Phase 6: Final Clean-upGo to System > Settings > General. Ensure DNS Servers is empty or set to 127.0.0.1.Go to Services > DHCPv4 > [LAN]. Ensure the DNS servers field is empty (it will then automatically give clients your router's IP for DNS).[!IMPORTANT]Validation: Go to the AdGuard Dashboard. If you see "Total Queries" rising, it's working. If you see device names like iphone.local instead of just IPs, your Unbound link is perfect.