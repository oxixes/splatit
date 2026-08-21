import type { Route } from "./+types/lobbies"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Users, MapPin, Gamepad2, RefreshCw} from "lucide-react";
import { Button } from "~/components/ui/button";
import { useEffect, useState } from "react";
import { useAppConfig } from "~/hooks/useAppConfig";
import { getLobbies, getGameModeName } from "~/lib/splatoon";
import type { Lobby } from "~/lib/splatoon";
import { LobbyDetailsDialog } from "~/components/lobbies/LobbyDetailsDialog";
import { ActiveLobbiesCard } from "~/components/stats/ActiveLobbiesCard";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Lobbies - SplatIt Server" },
    { name: "description", content: "SplatIt Splatoon server recreation admin UI." },
  ]
}

const PRIVATE_LOBBY_GAME_MODE = 3;

export default function Lobbies() {
  const { config } = useAppConfig();
  const [lobbies, setLobbies] = useState<Lobby[]>([]);
  const [loading, setLoading] = useState(false);
  const [refreshing, setRefreshing] = useState(false);
  const [selectedLobby, setSelectedLobby] = useState<Lobby | null>(null);
  const [detailsOpen, setDetailsOpen] = useState(false);

  const REFRESH_INTERVAL = 60000; // 1 minute

  const loadLobbies = async () => {
    try {
      setLoading(true);
      const res = await getLobbies(config);
      setLobbies(res.lobbies);
    } catch (e) {
      console.error("Failed to load lobbies:", e);
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  };

  const handleRefresh = async () => {
    setRefreshing(true);
    await loadLobbies();
  };

  useEffect(() => {
    void loadLobbies();

    const intervalId = setInterval(() => {
      void loadLobbies();
    }, REFRESH_INTERVAL);

    return () => clearInterval(intervalId);
  }, [config]);

  const handleLobbyClick = (lobby: Lobby) => {
    setSelectedLobby(lobby);
    setDetailsOpen(true);
  };

  const privateLobbies = lobbies.filter(l => l.gameMode === PRIVATE_LOBBY_GAME_MODE);
  const regularPlayLobbies = lobbies.filter(l => l.gameMode !== PRIVATE_LOBBY_GAME_MODE);

  return (
      <div className="space-y-6">
          <div className="flex items-center justify-between">
              <div>
                  <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">
                      Active Lobbies
                  </h1>
                  <p className="text-muted-foreground mt-2">
                      Monitor active game lobbies
                  </p>
              </div>
          </div>

          {/* Stats Grid */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
              <ActiveLobbiesCard />

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Regular Play</CardTitle>
                      <div className="flex items-center gap-2">
                          <Button
                              variant="ghost"
                              size="sm"
                              onClick={handleRefresh}
                              disabled={refreshing}
                              className="h-6 w-6 p-0"
                          >
                              <RefreshCw className={`h-3 w-3 ${refreshing ? 'animate-spin' : ''}`} />
                          </Button>
                          <MapPin className="h-4 w-4 text-muted-foreground" />
                      </div>
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">{regularPlayLobbies.length}</div>
                      <p className="text-xs text-muted-foreground">Public lobbies</p>
                  </CardContent>
              </Card>

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Private Lobbies</CardTitle>
                      <div className="flex items-center gap-2">
                          <Button
                              variant="ghost"
                              size="sm"
                              onClick={handleRefresh}
                              disabled={refreshing}
                              className="h-6 w-6 p-0"
                          >
                              <RefreshCw className={`h-3 w-3 ${refreshing ? 'animate-spin' : ''}`} />
                          </Button>
                          <Users className="h-4 w-4 text-muted-foreground" />
                      </div>
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">{privateLobbies.length}</div>
                      <p className="text-xs text-muted-foreground">Password protected</p>
                  </CardContent>
              </Card>
          </div>

          {/* Active Lobbies List */}
          <Card>
              <CardHeader>
                  <div className="flex items-center justify-between">
                    <div>
                      <CardTitle>Active Lobbies</CardTitle>
                      <CardDescription>
                          Lobbies currently active on the server. Players decide which map to play from the rotation.
                      </CardDescription>
                    </div>
                    <Button
                      variant="outline"
                      size="sm"
                      onClick={handleRefresh}
                      disabled={refreshing}
                    >
                      <RefreshCw className={`h-4 w-4 mr-2 ${refreshing ? 'animate-spin' : ''}`} />
                      Refresh
                    </Button>
                  </div>
              </CardHeader>
              <CardContent>
                  {loading && lobbies.length === 0 ? (
                    <div className="text-center py-12 text-muted-foreground">
                      <RefreshCw className="h-12 w-12 mx-auto mb-4 opacity-50 animate-spin" />
                      <p className="text-lg font-medium">Loading lobbies...</p>
                    </div>
                  ) : lobbies.length === 0 ? (
                    <div className="text-center py-12 text-muted-foreground">
                      <Gamepad2 className="h-12 w-12 mx-auto mb-4 opacity-50" />
                      <p className="text-lg font-medium">No active lobbies</p>
                      <p className="text-sm mt-2">
                          Lobbies will appear here when players create or join them
                      </p>
                    </div>
                  ) : (
                    <div className="overflow-x-auto">
                      <table className="w-full">
                        <thead>
                          <tr className="border-b">
                            <th className="py-3 px-2 text-left text-sm font-medium">Gathering ID</th>
                            <th className="py-3 px-2 text-left text-sm font-medium">Game Mode</th>
                            <th className="py-3 px-2 text-left text-sm font-medium">Owner PID</th>
                            <th className="py-3 px-2 text-left text-sm font-medium">Players</th>
                            <th className="py-3 px-2 text-left text-sm font-medium">Started</th>
                          </tr>
                        </thead>
                        <tbody>
                          {lobbies.map((lobby) => (
                            <tr
                              key={lobby.gId}
                              className="border-b last:border-0 hover:bg-muted/50 cursor-pointer"
                              onClick={() => handleLobbyClick(lobby)}
                            >
                              <td className="py-3 px-2 font-mono text-xs">{lobby.gId}</td>
                              <td className="py-3 px-2 text-sm">{getGameModeName(lobby.gameMode)}</td>
                              <td className="py-3 px-2 font-mono text-xs">{lobby.ownerPid}</td>
                              <td className="py-3 px-2 text-sm">
                                {lobby.playerPids.length} / {lobby.maxParticipants}
                                {lobby.minParticipants > 1 && ` (min: ${lobby.minParticipants})`}
                              </td>
                              <td className="py-3 px-2 text-sm">
                                {new Date(lobby.startedTime * 1000).toLocaleString()}
                              </td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                  )}
              </CardContent>
          </Card>

          <LobbyDetailsDialog
            config={config}
            lobby={selectedLobby}
            open={detailsOpen}
            onOpenChange={setDetailsOpen}
          />
      </div>
  )
}