import { useEffect, useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "~/components/ui/dialog";
import { Card, CardContent, CardHeader, CardTitle } from "~/components/ui/card";
import type { AppConfig } from "~/hooks/useAppConfig";
import type { Lobby } from "~/lib/splatoon";
import { getGameModeName } from "~/lib/splatoon";
import { getAccount } from "~/lib/accounts";
import type { Account } from "~/types/account";

export function LobbyDetailsDialog({
  config,
  lobby,
  open,
  onOpenChange,
}: {
  config: AppConfig;
  lobby: Lobby | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
}) {
  const [ownerAccount, setOwnerAccount] = useState<Account | null>(null);
  const [hostAccount, setHostAccount] = useState<Account | null>(null);
  const [playerAccounts, setPlayerAccounts] = useState<Record<number, Account>>({});
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!open || !lobby) {
      setOwnerAccount(null);
      setHostAccount(null);
      setPlayerAccounts({});
      return;
    }

    const loadAccounts = async () => {
      try {
        setLoading(true);

        // Load owner account
        const ownerRes = await getAccount(config, lobby.ownerPid);
        setOwnerAccount(ownerRes.account);

        // Load host account if different from owner
        if (lobby.hostPid !== lobby.ownerPid) {
          try {
            const hostRes = await getAccount(config, lobby.hostPid);
            setHostAccount(hostRes.account);
          } catch (e) {
            console.error(`Failed to load host account for PID ${lobby.hostPid}:`, e);
          }
        }

        // Load all player accounts
        const accounts: Record<number, Account> = {};
        await Promise.all(
          lobby.playerPids.map(async (pid) => {
            try {
              const res = await getAccount(config, pid);
              accounts[pid] = res.account;
            } catch (e) {
              console.error(`Failed to load account for PID ${pid}:`, e);
            }
          })
        );
        setPlayerAccounts(accounts);
      } catch (e) {
        console.error("Failed to load lobby accounts:", e);
      } finally {
        setLoading(false);
      }
    };

    void loadAccounts();
  }, [open, lobby, config]);

  if (!lobby) return null;

  const startedDate = new Date(lobby.startedTime * 1000);

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-2xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Lobby Details</DialogTitle>
          <DialogDescription>
            Gathering ID: {lobby.gId}
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle className="text-base">General Information</CardTitle>
            </CardHeader>
            <CardContent className="space-y-2 text-sm">
              <div className="grid grid-cols-2 gap-2">
                <div>
                  <span className="font-medium">Game Mode:</span> {getGameModeName(lobby.gameMode)}
                </div>
                <div>
                  <span className="font-medium">Started:</span> {startedDate.toLocaleString()}
                </div>
                <div>
                  <span className="font-medium">Open Participation:</span> {lobby.openParticipation ? "Yes" : "No"}
                </div>
              </div>
              <div className="border-t pt-2 mt-2 space-y-2">
                <div className="grid grid-cols-2 gap-2">
                  <div>
                    <div className="font-medium mb-1">Owner:</div>
                    <div className="pl-2 space-y-0.5">
                      <div className="font-mono text-xs">PID: {lobby.ownerPid}</div>
                      {loading ? (
                        <div className="text-muted-foreground text-xs">Loading...</div>
                      ) : ownerAccount ? (
                        <div className="text-xs">{ownerAccount.username}</div>
                      ) : (
                        <div className="text-muted-foreground text-xs">Account not found</div>
                      )}
                    </div>
                  </div>
                  <div>
                    <div className="font-medium mb-1">Host:</div>
                    <div className="pl-2 space-y-0.5">
                      <div className="font-mono text-xs">PID: {lobby.hostPid}</div>
                      {lobby.hostPid === lobby.ownerPid ? (
                        loading ? (
                          <div className="text-muted-foreground text-xs">Loading...</div>
                        ) : ownerAccount ? (
                          <div className="text-xs">{ownerAccount.username}</div>
                        ) : (
                          <div className="text-muted-foreground text-xs">Account not found</div>
                        )
                      ) : loading ? (
                        <div className="text-muted-foreground text-xs">Loading...</div>
                      ) : hostAccount ? (
                        <div className="text-xs">{hostAccount.username}</div>
                      ) : (
                        <div className="text-muted-foreground text-xs">Account not found</div>
                      )}
                    </div>
                  </div>
                </div>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle className="text-base">Participants</CardTitle>
            </CardHeader>
            <CardContent className="space-y-2 text-sm">
              <div className="grid grid-cols-3 gap-2 mb-3">
                <div>
                  <span className="font-medium">Current:</span> {lobby.playerPids.length}
                </div>
                <div>
                  <span className="font-medium">Min:</span> {lobby.minParticipants}
                </div>
                <div>
                  <span className="font-medium">Max:</span> {lobby.maxParticipants}
                </div>
              </div>

              {lobby.playerPids.length > 0 && (
                <div>
                  <div className="font-medium mb-2">Players:</div>
                  <div className="space-y-1">
                    {lobby.playerPids.map((pid) => (
                      <div key={pid} className="flex items-center gap-2 p-2 bg-muted/30 rounded">
                        <span className="font-mono text-xs">{pid}</span>
                        {loading ? (
                          <span className="text-muted-foreground">Loading...</span>
                        ) : playerAccounts[pid] ? (
                          <span>{playerAccounts[pid].username}</span>
                        ) : (
                          <span className="text-muted-foreground">Unknown</span>
                        )}
                      </div>
                    ))}
                  </div>
                </div>
              )}
            </CardContent>
          </Card>

          {lobby.description && (
            <Card>
              <CardHeader>
                <CardTitle className="text-base">Description</CardTitle>
              </CardHeader>
              <CardContent>
                <p className="text-sm">{lobby.description}</p>
              </CardContent>
            </Card>
          )}

          <Card>
            <CardHeader>
              <CardTitle className="text-base">Additional Details</CardTitle>
            </CardHeader>
            <CardContent className="space-y-1 text-sm">
              <div>
                <span className="font-medium">Matchmake System Type:</span> {lobby.matchmakeSystemType}
              </div>
              <div>
                <span className="font-medium">Participation Policy:</span> {lobby.participationPolicy}
              </div>
              <div>
                <span className="font-medium">Flags:</span> {lobby.flags}
              </div>
              <div>
                <span className="font-medium">System Password:</span> {lobby.systemPasswordEnabled ? "Enabled" : "Disabled"}
              </div>
              <div>
                <span className="font-medium">User Password:</span> {lobby.userPasswordEnabled ? "Enabled" : "Disabled"}
              </div>
            </CardContent>
          </Card>
        </div>
      </DialogContent>
    </Dialog>
  );
}


