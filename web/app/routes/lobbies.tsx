import type { Route } from "./+types/lobbies"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Users, MapPin, Gamepad2} from "lucide-react";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Lobbies - SplatIt Server" },
    { name: "description", content: "SplatIt Splatoon server recreation admin UI." },
  ]
}

export default function Lobbies() {
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
              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Active Lobbies</CardTitle>
                      <Gamepad2 className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">Currently active</p>
                  </CardContent>
              </Card>

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Regular Play</CardTitle>
                      <MapPin className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">With map rotation</p>
                  </CardContent>
              </Card>

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Private Lobbies</CardTitle>
                      <Users className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">Custom matches</p>
                  </CardContent>
              </Card>
          </div>

          {/* Active Lobbies List */}
          <Card>
              <CardHeader>
                  <CardTitle>Active Lobbies</CardTitle>
                  <CardDescription>
                      Lobbies currently active on the server. Players decide which map to play from the rotation.
                  </CardDescription>
              </CardHeader>
              <CardContent>
                  <div className="text-center py-12 text-muted-foreground">
                      <Gamepad2 className="h-12 w-12 mx-auto mb-4 opacity-50" />
                      <p className="text-lg font-medium">No active lobbies</p>
                      <p className="text-sm mt-2">
                          Lobbies will appear here when players create or join them
                      </p>
                  </div>
              </CardContent>
          </Card>

          {/* Info Card */}
          <Card className="border-blue-500/50 bg-blue-500/5">
              <CardHeader>
                  <CardTitle className="text-blue-500">Lobby System Information</CardTitle>
              </CardHeader>
              <CardContent className="space-y-2 text-sm">
                  <p>• <strong>Regular Play:</strong> Lobbies use the current map rotation from BOSS server</p>
                  <p>• <strong>Private Lobbies:</strong> Players can choose any map</p>
                  <p>• <strong>Map Selection:</strong> Players vote on maps - server doesn't track individual matches</p>
                  <p>• <strong>Multiple Matches:</strong> Each lobby can play several matches before disbanding</p>
              </CardContent>
          </Card>
      </div>
  )
}