import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "~/components/ui/card";
import { Badge } from "~/components/ui/badge";
import { Button } from "~/components/ui/button";
import { RefreshCw, Server } from "lucide-react";
import { Skeleton } from "~/components/ui/skeleton";
import { useServerStatusData } from "~/contexts/ServerStatusContext";
import type { ServerInfo } from "~/contexts/ServerStatusContext";

const SERVER_TYPE_NAMES: Record<string, { name: string; description: string }> = {
  account: { name: "Account Server", description: "Authentication & account management" },
  boss: { name: "BOSS Server", description: "Festival data & map rotation" },
  friends_auth: { name: "Friends Auth Server", description: "Friends service authentication" },
  friends_secure: { name: "Friends Server", description: "Friendship management" },
  splatoon_auth: { name: "Splatoon Auth Server", description: "Game authentication" },
  splatoon_secure: { name: "Splatoon Server", description: "Game server & lobbies" },
};

function ServerStatusSkeleton() {
  return (
    <div className="space-y-3">
      {[1, 2, 3, 4, 5, 6].map((i) => (
        <div key={i} className="flex items-center justify-between border-b pb-3 last:border-0 last:pb-0">
          <div className="flex items-center gap-3">
            <Skeleton className="h-8 w-8 rounded-full" />
            <div className="space-y-2">
              <Skeleton className="h-4 w-32" />
              <Skeleton className="h-3 w-48" />
            </div>
          </div>
          <Skeleton className="h-6 w-24" />
        </div>
      ))}
    </div>
  );
}

interface ServerStatusCardProps {
  showTitle?: boolean;
  className?: string;
}

export function ServerStatusCard({ showTitle = true, className }: ServerStatusCardProps) {
  const { servers, loading, error, refreshing, refresh } = useServerStatusData();

  // Group servers by type
  const serversByType = servers.reduce((acc, server) => {
    if (!acc[server.type]) {
      acc[server.type] = [];
    }
    acc[server.type].push(server);
    return acc;
  }, {} as Record<string, ServerInfo[]>);

  return (
    <Card className={className}>
      <CardHeader>
        <div className="flex items-center justify-between">
          <div>
            {showTitle && <CardTitle>Server Status</CardTitle>}
            <CardDescription>Status of server components</CardDescription>
          </div>
          <Button
            variant="outline"
            size="icon"
            onClick={refresh}
            disabled={loading || refreshing}
            title="Refresh server status"
          >
            <RefreshCw className={`h-4 w-4 ${refreshing ? "animate-spin" : ""}`} />
          </Button>
        </div>
      </CardHeader>
      <CardContent>
        {loading || refreshing ? (
          <ServerStatusSkeleton />
        ) : error ? (
          <div className="text-center py-8 text-muted-foreground">
            <Server className="h-12 w-12 mx-auto mb-4 opacity-50" />
            <p className="text-sm">Failed to load server status</p>
            <p className="text-xs mt-1">{error}</p>
            <Button variant="outline" size="sm" onClick={refresh} className="mt-4">
              Try Again
            </Button>
          </div>
        ) : servers.length === 0 ? (
          <div className="text-center py-8 text-muted-foreground">
            <Server className="h-12 w-12 mx-auto mb-4 opacity-50" />
            <p className="text-sm">No servers found</p>
          </div>
        ) : (
          <div className="space-y-3">
            {Object.entries(serversByType).map(([type, serverList]) => {
              const typeInfo = SERVER_TYPE_NAMES[type] || {
                name: type,
                description: "Unknown service",
              };

              // Calculate online/offline counts for this type
              const onlineCount = serverList.filter(s => s.status === "online").length;
              const totalCount = serverList.length;
              const allOffline = onlineCount === 0;
              const someOnline = onlineCount > 0 && onlineCount < totalCount;

              // Get offline servers with messages
              const offlineServers = serverList.filter(s => s.status === "offline");

              // Determine status color
              let statusColor = "text-green-500";
              let bgColor = "bg-green-500/10";
              let dotColor = "bg-green-500";

              if (allOffline) {
                statusColor = "text-red-500";
                bgColor = "bg-red-500/10";
                dotColor = "bg-red-500";
              } else if (someOnline) {
                statusColor = "text-yellow-500";
                bgColor = "bg-yellow-500/10";
                dotColor = "bg-yellow-500";
              }

              return (
                <div
                  key={type}
                  className="flex items-start justify-between border-b pb-3 last:border-0 last:pb-0"
                >
                  <div className="flex items-start gap-3 flex-1">
                    <div className={`flex h-8 w-8 items-center justify-center rounded-full ${bgColor} flex-shrink-0 mt-0.5`}>
                      <div className={`h-2 w-2 rounded-full ${dotColor}`} />
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center justify-between gap-3">
                        <div className="flex-1 min-w-0">
                          <p className="font-medium text-sm">{typeInfo.name}</p>
                          <p className="text-xs text-muted-foreground">
                            {typeInfo.description}
                          </p>
                        </div>
                        <Badge
                          variant="outline"
                          className={`${statusColor} flex-shrink-0`}
                        >
                          {onlineCount}/{totalCount} online
                        </Badge>
                      </div>
                      {offlineServers.length > 0 && (
                        <div className="mt-2 space-y-1">
                          {offlineServers.map((server, index) => (
                            <div key={index} className="text-xs text-red-400">
                              <span className="font-medium">{server.address}:</span> {server.message || "Offline"}
                            </div>
                          ))}
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </CardContent>
    </Card>
  );
}

