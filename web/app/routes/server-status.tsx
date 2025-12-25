import type { Route } from "./+types/server-status"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Server} from "lucide-react";
import {Badge} from "~/components/ui/badge";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Server Status - SplatIt Server" },
    { name: "description", content: "Server status overview." },
  ]
}

export default function ServerStatus() {
  return (
      <div className="space-y-6">
          <div className="flex items-center justify-between">
              <div>
                  <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">
                      Server Status
                  </h1>
                  <p className="text-muted-foreground mt-2">
                      Distributed architecture status
                  </p>
              </div>
              <Badge variant="outline" className="h-8 px-3">
                  <div className="flex items-center gap-2">
                      <div className="h-2 w-2 rounded-full bg-green-500 animate-pulse" />
                      All Systems Online
                  </div>
              </Badge>
          </div>

          <Card>
              <CardHeader>
                  <CardTitle>Server Architecture</CardTitle>
                  <CardDescription>Status of distributed server components</CardDescription>
              </CardHeader>
              <CardContent>
                  <div className="space-y-3">
                      {[
                          { name: "Account Server", description: "Authentication & account management" },
                          { name: "Friends Auth Server", description: "Friends service authentication" },
                          { name: "Friends Server", description: "Friendship management" },
                          { name: "Splatoon Auth Server", description: "Game authentication" },
                          { name: "Splatoon Server", description: "Game server & lobbies" },
                          { name: "BOSS Server", description: "Festival data & map rotation" },
                          { name: "Management UI Server", description: "Admin interface (gRPC)" },
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
                                  operational
                              </Badge>
                          </div>
                      ))}
                  </div>
              </CardContent>
          </Card>

          <Card className="border-blue-500/50 bg-blue-500/5">
              <CardHeader>
                  <CardTitle className="text-blue-500">Distributed Architecture</CardTitle>
              </CardHeader>
              <CardContent className="text-sm space-y-2">
                  <p>• All servers communicate via <strong>gRPC</strong></p>
                  <p>• Servers can run on the same machine or distributed across multiple hosts</p>
                  <p>• Authentication and game servers support load balancing (future feature)</p>
                  <p>• Each server manages its own database independently</p>
              </CardContent>
          </Card>
      </div>
  )
}

