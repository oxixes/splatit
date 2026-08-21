# Roadmap

Nothing here is scheduled. It is a list of the gaps worth closing.

- **Authorisation is one bit.** An account is an administrator or it is not.
  There is no read-only operator, no per-area permission and no audit trail of
  who changed what.
- **The Windows time zone database needs a manual download.** `CMakeLists.txt`
  has carried a TODO about automating it for a while.
- **The management UI is separated from the API.** The management UI is a separate 
  web application that talks to the API. It would be nice to have the application 
  serve both the API and the management UI.
- **The NAT traversal servers are not implemented.** The NAT traversal servers are 
  not implemented, so the system cannot traverse NATs without using the Nintendo ones. 
  These are super simple servers that can be easily implemented.
- **SplatNet is not implemented.** The game supports publishing data to be shown on SplatNet,
  a website that shows statistics and other information about the game. The game can be played 
  without SplatNet, but it is a nice feature to have.

## Not Panned

The following are not planned to be implemented as they require a lot of work and the game does not need them.

- **Miiverse.** The game just uses it for simple messages in the lobby, so developing a whole social network
  just for that is not worth it. The game will just not have that feature.
- **Mii generation.** The game uses Miis for the player avatars, but it is not worth implementing a Mii generator just for that.
  A default Mii image will be used instead. If you want to use your own Mii, you can provide your image and it will be used instead of the default one.