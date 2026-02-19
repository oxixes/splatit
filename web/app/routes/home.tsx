import type { Route } from "./+types/home"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Server} from "lucide-react";
import {Badge} from "~/components/ui/badge";
import { ServerStatusCard } from "~/components/server-status-card";
import { useServerStatusData } from "~/contexts/ServerStatusContext";
import { TotalAccountsCard } from "~/components/stats/TotalAccountsCard";
import { ActivePlayersCard } from "~/components/stats/ActivePlayersCard";
import { ActiveLobbiesCard } from "~/components/stats/ActiveLobbiesCard";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Home - SplatIt Server" },
    { name: "description", content: "SplatIt Splatoon server recreation admin UI." },
  ]
}

export default function Home() {
  const { servers, loading } = useServerStatusData();

  // Calculate overall system status (worst case scenario)
  const getSystemStatus = () => {
    if (loading || servers.length === 0) {
      return { color: "bg-gray-500", text: "Loading...", pulse: false };
    }

    // Group servers by type
    const serversByType = servers.reduce((acc, server) => {
      if (!acc[server.type]) {
        acc[server.type] = [];
      }
      acc[server.type].push(server);
      return acc;
    }, {} as Record<string, typeof servers>);

    // Check status for each type
    let hasAllOffline = false;
    let hasSomeOffline = false;

    Object.values(serversByType).forEach((serverList) => {
      const onlineCount = serverList.filter(s => s.status === "online").length;
      const totalCount = serverList.length;

      if (onlineCount === 0) {
        hasAllOffline = true;
      } else if (onlineCount < totalCount) {
        hasSomeOffline = true;
      }
    });

    // Return worst case status
    if (hasAllOffline) {
      return { color: "bg-red-500", text: "System Critical", pulse: true };
    } else if (hasSomeOffline) {
      return { color: "bg-yellow-500", text: "System Degraded", pulse: true };
    } else {
      return { color: "bg-green-500", text: "System Online", pulse: true };
    }
  };

  const systemStatus = getSystemStatus();

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
                      <div className={`h-2 w-2 rounded-full ${systemStatus.color} ${systemStatus.pulse ? 'animate-pulse' : ''}`} />
                      {systemStatus.text}
                  </div>
              </Badge>
          </div>

          {/* Main Stats Grid */}
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
              <ActivePlayersCard />

              <TotalAccountsCard />

              <ActiveLobbiesCard />
          </div>

          {/* Distributed Architecture Status */}
          <ServerStatusCard showTitle={true} />

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