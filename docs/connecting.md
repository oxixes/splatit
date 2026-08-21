# Connecting a client

Two paths lead into the server. Cemu reads its service URLs from a
configuration file, so it needs no DNS tricks. A real Wii U has the Nintendo
domains compiled in, so it needs DNS redirection and a trusted CA.

## Creating an account

Every client needs an account before it can sign in. Create one from the admin
panel:

1. Open the panel and sign in as an administrator.
2. Go to **Players** and select **Create player**.
3. Fill in the username, password, birth date, country and region. The server
   assigns the PID.
4. Save. The account exists immediately, and with `accounts.email.enabled` set to
   `false` the address counts as verified straight away.

```{figure} _static/img/players-create.png
:alt: The create player dialog in the admin panel
:width: 100%

Creating an account from the Players page.
```

The console can also register an account by itself through the system settings,
as long as `allowRealWiiU` or `allowGeneratedWiiU` permits its certificate.

## Cemu

### 1. Download the file bundle

The management API generates everything Cemu needs to look like a console tied
to a specific account.

1. Open **Players**, find the account and select **Download Cemu files**.
2. Enter the account password. The server needs it to build `account.dat`.
3. Save the ZIP archive.

```{figure} _static/img/players-cemu-download.png
:alt: The Cemu file download dialog
:width: 100%

Generating the emulator file bundle for an account.
```

The archive contains:

```text
otp.bin                                          generated console OTP
seeprom.bin                                      generated console SEEPROM
network_services.xml                             service URLs pointing at this server
mlc01/usr/save/system/act/<persistentId>/account.dat
mlc01/sys/title/0005001b/10054000/content/ccerts/WIIU_*_CERT.der
mlc01/sys/title/0005001b/10054000/content/ccerts/WIIU_*_RSA_KEY.aes
mlc01/sys/title/0005001b/10054000/content/scerts/*.der
```

The `ccerts` files are the device certificate and key that the account server
issued, which is how it recognises the client. The `scerts` files are copies of
the server CA, which is how the client comes to trust the HTTPS certificate.

```{admonition} Handle the bundle carefully
:class: warning

The archive contains a private key and a hashed account password. Anyone holding
it can sign in as that account. Generate one per account, and do not share them.
```

### 2. Install it

1. Close Cemu.
2. Copy `otp.bin`, `seeprom.bin` and `network_services.xml` into the Cemu
   directory, next to `Cemu.exe`.
3. Merge the `mlc01` folder from the archive into Cemu's `mlc01` directory,
   keeping the existing structure.
4. Start Cemu and open **Options, General settings, Account**.
5. Pick the account from the bundle, set the online mode to **Custom** or
   **Network service: Custom**, depending on the Cemu version, and confirm that
   the account shows as online-ready.

The URLs may still use the `nintendo.net` names (depending on your configuration), so Cemu's host still has to
resolve them to the server. On a LAN, a hosts file entry is enough:

```text
192.168.1.10 account.nintendo.net
192.168.1.10 mii-secure.account.nintendo.net
192.168.1.10 npts.app.nintendo.net
192.168.1.10 npdi.cdn.nintendo.net
192.168.1.10 nppl.app.nintendo.net
```

Changing `domain` in the settings makes the generator emit that domain instead,
which is the cleaner option when a real domain is available.

### 3. Play

Start Splatoon. The game signs in through the account server, receives a NEX
token, connects to the auth server and then to the secure server. The **Lobbies**
page in the admin panel shows the session appearing.

## A real Wii U

### TODO

## Troubleshooting

| Symptom                                             | Likely cause                                                                                                      |
|-----------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| The emulator cannot reach the account server at all | DNS still resolves the real Nintendo addresses, port 443 is blocked, or you did not copy the correct certificates |
| Sign in succeeds, the game reports maintenance      | The security status is set to maintenance mode in the admin panel                                                 |
| Friends never come online                           | The friends secure server is disabled, or two nodes share a `nex.serverId`                                        |
| Players connect but never see each other's lobbies  | Two secure servers are running against `sharedState.type` set to `local`                                          |

Raise the log level to 0 with `--log-level 0` to see individual PRUDP packets and
RMC calls. It is verbose, and it makes protocol level problems obvious.
