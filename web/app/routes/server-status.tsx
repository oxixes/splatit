import type { Route } from "./+types/server-status"
import {Card, CardContent, CardHeader, CardTitle} from "~/components/ui/card";
import { ServerStatusCard } from "~/components/server-status-card";

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
          </div>

          <ServerStatusCard showTitle={true} />

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

