# XeLL native HTTP/authentication build 0.2

This extends the existing hardware-tested libXenon ELF. The renderer/console, USB controller polling, audio tone, Ethernet driver and original four diagnostic pages remain. It adds two pages, a controller keyboard and native HTTP. No desktop UI replacement or video-decoder work is included in this stage.

The user has confirmed **the earlier baseline ELF** boots through XeLL, renders graphics, accepts controller navigation, plays audio and initializes Ethernet with link up and IP `192.168.68.112`. That is valuable hardware evidence. It does **not** prove that the earlier address was DHCP-assigned, or that this updated build has passed its new hardware tests.

## Download and test

Download `dist/xenonfin-xell-http.elf`, copy it to the FAT32 USB root as **`xenon.elf`**, and launch through your existing working XeLL setup. The historical `xenonfin-xbox-boot-diagnostic.tar.gz` download also contains the updated file under its existing archive name. No `default.xex` is produced. Keep your old working ELF for rollback; the original version is also retained in Git at commit `c7d293b`.

1. **Network / DHCP diagnostics:** press A. The existing PHY initialization runs first with a 12-second watchdog. DHCP then progresses while the controller and interface continue to run. It gets a separate 20-second diagnostic timeout. A retries DHCP without initializing the hardware or lwIP a second time. No static fallback address is assigned.
2. Confirm **`ACK lease verified: YES`**, normally with **`DHCP state: BOUND (10)`**. The screen also shows the DHCP-managed flag, interface-up flag, configured IP/netmask/gateway, DHCP server, offer/ACK address, lease duration and retry count. `192.168.68.112` being printed is insufficient by itself. RENEWING/REBINDING are accepted only while retaining matching ACK lease data. A pending state or mismatched address keeps HTTP disabled.
3. **Jellyfin server:** Y opens the keyboard. Enter your **server's** address, for example `192.168.68.202:8096`, then Start or the keyboard's DONE key. `192.168.68.112` is the reported Xbox address, not automatically the server address. If omitted, port 8096 is used. A tests **`GET /System/Info/Public`**.
4. Inspect HTTP status, network errors, server name and version. X toggles the first 420 bytes of the public response body. Control/escape characters are sanitized. A normal result is `HTTP 200: Jellyfin connection verified`. B cancels an active request. You can change pages while it runs.
5. **Jellyfin login:** after the public-info check succeeds, select Username with A, enter it and finish. Select Password with A; entry is masked. Select Sign in with A. The client sends **`POST /Users/AuthenticateByName`** with JSON `Username` and `Pw`, and Jellyfin's `Authorization: MediaBrowser ...` client identification header. A successful response must contain a nonempty `AccessToken` and `User.Id`; the user name is displayed. Authentication bodies and tokens are never displayed, including error responses. 401/403 and network failures are shown explicitly. Re-enter the password for each retry: it is cleared on submission.
6. Log out erases the local RAM token/user. Server address, credentials and session state are **not persisted** in this build. Editing the server clears the prior verified connection and session. Changing username/password clears the prior session. Normal operation is controller-only.

Keyboard: D-pad/left stick moves, A types, X deletes, Y switches symbols, SHIFT changes case, Start/DONE accepts and B cancels. All printable ASCII characters are available, capped at 128 characters. LB/RB changes pages outside the keyboard. The original audio page's A still plays its 440 Hz test tone. B returns from login to the server page; outside keyboard/request/login it retains the existing return-to-XeLL behavior.

**Transport scope:** numeric IPv4 HTTP, including optional `http://` and a port. This pinned stack has `LWIP_DNS=0`, `LWIP_SOCKET=0`, `LWIP_NETCONN=0`, `NO_SYS=1`, callback TCP and DHCP. There is no native TLS backend; `https://` is explicitly rejected rather than downgraded. Use this build's authentication only on your trusted LAN. The public HTTPS hostname remains supported by the separate desktop client, not by this native build. Reverse-proxy base paths and redirects are not followed in this stage.

## What changed in networking

The bundled `network_init()` polls in a blocking DHCP wait and falls back to `192.168.1.99` on failure. Its success path checks a nonzero address. Instead of altering that library function or replacing the working Ethernet driver, the application invokes the same `lwip_init`, `netif_add(...enet_init...)`, `netif_set_default` and `dhcp_start` sequence without its DHCP wait/static fallback. The existing `network_poll()` continues from the main loop regardless of pending/failed DHCP status. ARP and fragment-reassembly timers, omitted by the old manual poll helper, are also serviced.

Readiness requires the interface's DHCP-managed flag, link flag, interface-up flag, BOUND/RENEWING/REBINDING state, nonzero assigned address equal to the offered/ACK address, and a nonzero lease duration. BOUND is entered by this lwIP implementation after accepting DHCP ACK and completing its ARP conflict check. This is protocol-state evidence beyond the displayed IP, not packet-capture evidence. The driver only sets its link flag during initialization; that flag is not advertised as continuous PHY monitoring. Cable loss during a request is detected through TCP errors/timeouts, even if the old flag still says UP.

HTTP uses raw lwIP callbacks, not unsupported sockets or a new scheduler. The loop continues controller/render work between network polls. TCP buffers use COPY ownership. Callbacks consume/free pbufs and mark completion; cleanup closes/aborts the PCB after returning from the callback. Error callbacks know lwIP has already freed the PCB. Connect and idle deadlines are 8 seconds, total request deadline 20 seconds, with elapsed subtraction safe across clock wrap. Public/credential JSON is bounded to 32 KiB; headers to 8 KiB; request storage to 4 KiB. Content-Length, chunked and connection-close framing are supported; ambiguous/truncated/oversized/compressed messages fail explicitly. No redirects or credential-bearing cross-origin requests are followed.

Two small adapters address busy waits in the **pinned, unchanged** Ethernet implementation:

- `driver_guard.c` checks TX descriptor ownership before calling the original `linkoutput`. A busy DMA slot returns ERR_MEM for retry rather than entering the driver's unbounded wait (or overwriting an in-flight buffer). Its private context offsets were checked against the actual pinned `enet.o` disassembly: TX pointer +12, index +16, 16 slots, 1528-byte slot size. Changing the libXenon image requires revalidating this adapter.
- `init_guard.c` uses linker `--wrap=mdelay` only to watch the original PHY/MDIO initialization's delay calls. It aborts that C initialization after 12 seconds if it cannot complete. Other delay calls pass through normally. Initial PHY setup can still temporarily block UI updates; DHCP and HTTP do not. A PHY timeout quiesces RX/TX, returns to diagnostics and **requires reboot**. Partially configured DMA allocations are deliberately retained rather than freed/reused after abandoning hardware initialization. This new guard still needs real-console verification.

The stable PowerPC timebase read checks the upper register twice to avoid false deadlines at lower-register rollover. Rendering is event-driven/rate-limited rather than continuously clearing the framebuffer. Token/request/password buffers are wiped when finished/cancelled where owned by this application; no secure-on-disk credential store is added.

## Build and verification

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
make -C platform/xenon/tests SANITIZE=1 test
python3 tests/native_server.py
```

The existing pinned toolchain and upstream ELF conversion are retained. The output remains `build/xenon/xenonfin-boot.elf32`; packaging exposes it as `dist/xenonfin-xell-http.elf`. Its name does not imply the completed music/video client.

Host tests exercise the exact C protocol/parser/keyboard and TCP callback code linked into the ELF. TCP, DHCP and driver-guard fault tests use host shims with the actual lwIP public types/signatures. They test state transitions, ownership, cancellation, deferred close, memory pressure, idle/total/connect deadlines and timer wrap; they do not exercise the Xbox NIC. Sanitized tests use AddressSanitizer and UndefinedBehaviorSanitizer. A separate real Jellyfin 10.10.7 integration test runs the native C request builder/response parser over host POSIX TCP: public info 200, login 200 including quote/backslash password escaping, token/user parsing, and wrong-password 401 passed. No personal server was modified and no video test is part of this stage.

Next hardware evidence needed: the new DHCP diagnostic state/lease values, HTTP status and public server name/body, masked text entry, successful login/401 recovery, and responsive cancellation against an unreachable IP/port. Do not include passwords or tokens in reports. Browsing/playback integration waits until these console connectivity/authentication checks work.
