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
      </div>
  )
}

