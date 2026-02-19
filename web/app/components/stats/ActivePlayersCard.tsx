import { Card, CardContent, CardHeader, CardTitle } from "~/components/ui/card";
import { Activity } from "lucide-react";
import { useSplatoonStats } from "~/contexts/SplatoonStatsContext";
import { Button } from "~/components/ui/button";
import { RefreshCw } from "lucide-react";

export function ActivePlayersCard() {
  const { clientCount, loading, refreshing, refresh } = useSplatoonStats();

  return (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
        <CardTitle className="text-sm font-medium">Active Players</CardTitle>
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
          <Activity className="h-4 w-4 text-muted-foreground" />
        </div>
      </CardHeader>
      <CardContent>
        <div className="text-2xl font-bold">{loading ? "..." : clientCount}</div>
        <p className="text-xs text-muted-foreground">Currently in-game</p>
      </CardContent>
    </Card>
  );
}

