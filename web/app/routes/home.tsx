import type { Route } from "./+types/home"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Activity, Users, Gamepad2, Server} from "lucide-react";
import {Badge} from "~/components/ui/badge";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Home - SplatIt Server" },
    { name: "description", content: "SplatIt Splatoon server recreation admin UI." },
  ]
}

export default function Home() {
  return (
      <div className="space-y-6">
          <div className="flex items-center justify-between">
              <div>
                  <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">
                      Server Dashboard
                  </h1>
                  <p className="text-muted-foreground mt-2">
                      Welcome to your Splatoon server management console
                  </p>
              </div>
              <Badge variant="outline" className="h-8 px-3">
                  <div className="flex items-center gap-2">
                      <div className="h-2 w-2 rounded-full bg-green-500 animate-pulse" />
                      System Online
                  </div>
              </Badge>
          </div>

          {/* Main Stats Grid */}
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Active Players</CardTitle>
                      <Activity className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">
                          Currently in-game
                      </p>
                  </CardContent>
              </Card>

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Total Accounts</CardTitle>
                      <Users className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">
                          Registered accounts
                      </p>
                  </CardContent>
              </Card>

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Active Lobbies</CardTitle>
                      <Gamepad2 className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">
                          Currently active
                      </p>
                  </CardContent>
              </Card>
          </div>

          {/* Distributed Architecture Status */}
          <Card>
              <CardHeader>
                  <CardTitle>Distributed Architecture Status</CardTitle>
                  <CardDescription>Status of all server components</CardDescription>
              </CardHeader>
              <CardContent>
                  <div className="space-y-3">
                      {[
                          { name: "Account Server", status: "operational", description: "Authentication & account management" },
                          { name: "Friends Auth Server", status: "operational", description: "Friends service authentication" },
                          { name: "Friends Server", status: "operational", description: "Friendship management" },
                          { name: "Splatoon Auth Server", status: "operational", description: "Game authentication" },
                          { name: "Splatoon Server", status: "operational", description: "Game server & lobbies" },
                          { name: "BOSS Server", status: "operational", description: "Festival data & map rotation" },
                          { name: "Management UI Server", status: "operational", description: "Admin interface (gRPC)" },
                      ].map((service) => (
                          <div key={service.name} className="flex items-center justify-between border-b pb-3 last:border-0 last:pb-0">
                              <div className="flex items-center gap-3">
                                  <div className="flex h-8 w-8 items-center justify-center rounded-full bg-green-500/10">
                                      <div className="h-2 w-2 rounded-full bg-green-500" />
                                  </div>
                                  <div>
                                      <p className="font-medium text-sm">{service.name}</p>
                                      <p className="text-xs text-muted-foreground">{service.description}</p>
                                  </div>
                              </div>
                              <Badge variant="outline" className="text-green-500">
                                  {service.status}
                              </Badge>
                          </div>
                      ))}
                  </div>
              </CardContent>
          </Card>

          {/* Recent Activity */}
          <Card>
              <CardHeader>
                  <CardTitle>Recent Activity</CardTitle>
                  <CardDescription>Latest server events</CardDescription>
              </CardHeader>
              <CardContent>
                  <div className="text-center py-8 text-muted-foreground">
                      <Server className="h-12 w-12 mx-auto mb-4 opacity-50" />
                      <p>No recent activity</p>
                      <p className="text-sm mt-2">Events will appear here when players connect</p>
                  </div>
              </CardContent>
          </Card>
      </div>
  )
}