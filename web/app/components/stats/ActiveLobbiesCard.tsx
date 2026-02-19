import { Card, CardContent, CardHeader, CardTitle } from "~/components/ui/card";
import { Gamepad2, RefreshCw } from "lucide-react";
import { useSplatoonStats } from "~/contexts/SplatoonStatsContext";
import { Button } from "~/components/ui/button";

export function ActiveLobbiesCard() {
  const { lobbyCount, loading, refreshing, refresh } = useSplatoonStats();

  return (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
        <CardTitle className="text-sm font-medium">Active Lobbies</CardTitle>
        <div className="flex items-center gap-2">
          <Button
            variant="ghost"
            size="sm"
            onClick={refresh}
            disabled={refreshing}
            className="h-6 w-6 p-0"
          >
            <RefreshCw className={`h-3 w-3 ${refreshing ? 'animate-spin' : ''}`} />
          </Button>
          <Gamepad2 className="h-4 w-4 text-muted-foreground" />
        </div>
      </CardHeader>
      <CardContent>
        <div className="text-2xl font-bold">{loading ? "..." : lobbyCount}</div>
        <p className="text-xs text-muted-foreground">Currently active</p>
      </CardContent>
    </Card>
  );
}

