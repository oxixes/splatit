import { useEffect, useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "~/components/ui/dialog";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "~/components/ui/table";
import { Award, Trophy, Users } from "lucide-react";
import type { AppConfig } from "~/hooks/useAppConfig";
import { getFestivalTotals } from "~/lib/splatoon";
import type { FestivalTeamTotal } from "~/types/splatoon";

export function FestivalResultsDialog({
  config,
  festivalId,
  teamAName,
  teamBName,
  open,
  onOpenChange,
}: {
  config: AppConfig;
  festivalId: number;
  teamAName: string;
  teamBName: string;
  open: boolean;
  onOpenChange: (open: boolean) => void;
}) {
  const [totals, setTotals] = useState<FestivalTeamTotal[] | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!open) return;

    setTotals(null);
    setError(null);

    const fetchTotals = async () => {
      setLoading(true);
      try {
        const response = await getFestivalTotals(config, festivalId);
        setTotals(response.totals);
      } catch (e) {
        setError(e instanceof Error ? e.message : "Failed to fetch festival totals.");
      } finally {
        setLoading(false);
      }
    };

    fetchTotals();
  }, [open, festivalId, config]);

  const teamName = (team: number) => {
    if (team === 0) return teamAName;
    if (team === 1) return teamBName;
    return `Team ${team}`;
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-lg">
        <DialogHeader>
          <DialogTitle>Festival #{festivalId} Results</DialogTitle>
          <DialogDescription>
            {teamAName} vs {teamBName}
          </DialogDescription>
        </DialogHeader>

        {loading && (
          <div className="py-8 text-center text-muted-foreground">
            Loading results...
          </div>
        )}

        {error && (
          <div className="py-4 text-center text-destructive text-sm">{error}</div>
        )}

        {totals && totals.length === 0 && (
          <div className="py-8 text-center text-muted-foreground">
            No results recorded for this festival yet.
          </div>
        )}

        {totals && totals.length > 0 && (
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>
                  <div className="flex items-center gap-1">
                    <Trophy className="h-3.5 w-3.5" />
                    Team
                  </div>
                </TableHead>
                <TableHead className="text-right">
                  <div className="flex items-center justify-end gap-1">
                    <Users className="h-3.5 w-3.5" />
                    Votes
                  </div>
                </TableHead>
                <TableHead className="text-right">
                  <div className="flex items-center justify-end gap-1">
                    <Award className="h-3.5 w-3.5" />
                    Wins
                  </div>
                </TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {totals.map((total) => (
                <TableRow key={total.team}>
                  <TableCell className="font-medium">{teamName(total.team)}</TableCell>
                  <TableCell className="text-right">{total.userCount.toLocaleString()}</TableCell>
                  <TableCell className="text-right">{total.totalWins.toLocaleString()}</TableCell>
                </TableRow>
              ))}
              <TableRow className="font-semibold bg-muted/50">
                <TableCell>Total</TableCell>
                <TableCell className="text-right">
                  {totals.reduce((sum, t) => sum + t.userCount, 0).toLocaleString()}
                </TableCell>
                <TableCell className="text-right">
                  {totals.reduce((sum, t) => sum + t.totalWins, 0).toLocaleString()}
                </TableCell>
              </TableRow>
            </TableBody>
          </Table>
        )}
      </DialogContent>
    </Dialog>
  );
}
