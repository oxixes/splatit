import type { Route } from "./+types/players"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "~/components/ui/card";
import { Users } from "lucide-react";
import { Button } from "~/components/ui/button";
import { Input } from "~/components/ui/input";
import { Label } from "~/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "~/components/ui/select";
import { useEffect, useMemo, useState } from "react";
import { useAppConfig } from "~/hooks/useAppConfig";
import type { Account, ListAccountsFilters, Pagination } from "~/types/account";
import { listAccounts } from "~/lib/accounts";
import { PlayerDetailsDialog } from "~/components/players/PlayerDetailsDialog";
import { CreatePlayerDialog } from "~/components/players/CreatePlayerDialog";
import { EditPlayerDialog } from "~/components/players/EditPlayerDialog";
import { TotalAccountsCard } from "~/components/stats/TotalAccountsCard";
import { SortableHeader } from "~/components/ui/sortable-header";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Players - SplatIt Server" },
    { name: "description", content: "Manage player accounts." },
  ]
}

export default function Players() {
  const { config } = useAppConfig();

  const [accounts, setAccounts] = useState<Account[]>([]);
  const [pagination, setPagination] = useState<Pagination>({ totalItems: 0, totalPages: 0, currentPage: 0 });
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [filters, setFilters] = useState<ListAccountsFilters>({
    page: 0,
    pageSize: 25,
    sort: "pid_desc",
  });

  const [searchUsername, setSearchUsername] = useState("");

  const [selectedPid, setSelectedPid] = useState<number | null>(null);
  const [detailsOpen, setDetailsOpen] = useState(false);
  const [editOpen, setEditOpen] = useState(false);

  const [createOpen, setCreateOpen] = useState(false);

  const loadAccounts = async () => {
    try {
      setLoading(true);
      setError(null);
      const res = await listAccounts(config, filters);
      setAccounts(res.accounts);
      setPagination(res.pagination);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Error loading accounts");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void loadAccounts();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [filters]);

  useEffect(() => {
    const handleOpenDevices = (e: Event) => {
      const customEvent = e as CustomEvent;
      if (customEvent.detail?.pid) {
        setSelectedPid(customEvent.detail.pid);
        setDetailsOpen(true);
      }
    };

    window.addEventListener('openPlayerDevices', handleOpenDevices);
    return () => window.removeEventListener('openPlayerDevices', handleOpenDevices);
  }, []);

  const openEdit = (pid?: number) => {
    if (pid == null) return;
    setSelectedPid(pid);
    setEditOpen(true);
  };

  const handleDetailsClose = (open: boolean) => {
    setDetailsOpen(open);
    if (!open && selectedPid) {
      // Reopen edit dialog when closing device management
      setTimeout(() => setEditOpen(true), 100);
    }
  };

  const handleSort = (key: string) => {
    const currentSort = filters.sort || "";
    let newSort = `${key}_desc`;
    if (currentSort.startsWith(key)) {
      newSort = currentSort.endsWith("_asc") ? `${key}_desc` : `${key}_asc`;
    }
    setFilters({ ...filters, sort: newSort, page: 0 });
  };

  const canPrev = useMemo(() => (filters.page ?? 0) > 0, [filters.page]);
  const canNext = useMemo(() => {
    if (pagination.totalPages === 0) return false;
    return (filters.page ?? 0) + 1 < pagination.totalPages;
  }, [filters.page, pagination.totalPages]);

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">Player Accounts</h1>
          <p className="text-muted-foreground mt-2">View and manage registered player accounts</p>
        </div>
        <Button onClick={() => setCreateOpen(true)}>Create User</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <TotalAccountsCard />

        <Card>
          <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
            <CardTitle className="text-sm font-medium">Active Now</CardTitle>
            <Users className="h-4 w-4 text-muted-foreground" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">—</div>
            <p className="text-xs text-muted-foreground">Not implemented (requires presence tracking)</p>
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <div className="flex flex-col md:flex-row md:items-end md:justify-between gap-3">
            <div>
              <CardTitle>Account List</CardTitle>
              <CardDescription>All registered player accounts from Account Server</CardDescription>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-2">
              <div className="space-y-1">
                <Label>Username</Label>
                <Input
                  value={searchUsername}
                  onChange={(e) => setSearchUsername(e.target.value)}
                  placeholder="Search..."
                />
              </div>
              <div className="space-y-1">
                <Label>Page size</Label>
                <Select
                  value={String(filters.pageSize ?? 25)}
                  onValueChange={(val) => setFilters({ ...filters, pageSize: Number(val), page: 0 })}
                >
                  <SelectTrigger className="w-full">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="10">10</SelectItem>
                    <SelectItem value="25">25</SelectItem>
                    <SelectItem value="50">50</SelectItem>
                    <SelectItem value="100">100</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="flex gap-2 md:justify-end md:items-end">
                <Button
                  variant="outline"
                  onClick={() => setFilters({ ...filters, username: searchUsername.trim() || undefined, page: 0 })}
                >
                  Apply
                </Button>
                <Button
                  variant="outline"
                  onClick={() => {
                    setSearchUsername("");
                    setFilters({ ...filters, username: undefined, page: 0 });
                  }}
                >
                  Clear
                </Button>
              </div>
            </div>
          </div>
        </CardHeader>

        <CardContent className="space-y-3">
          {error ? <div className="text-sm text-destructive">{error}</div> : null}

          {loading ? (
            <div className="text-sm text-muted-foreground py-8">Loading accounts...</div>
          ) : accounts.length === 0 ? (
            <div className="text-center py-12 text-muted-foreground">
              <Users className="h-12 w-12 mx-auto mb-4 opacity-50" />
              <p className="text-lg font-medium">No accounts found</p>
              <p className="text-sm mt-2">Try adjusting filters or create a user.</p>
            </div>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead>
                  <tr className="border-b">
                    <SortableHeader label="PID" sortKey="pid" currentSort={filters.sort} onSort={handleSort} />
                    <SortableHeader label="Username" sortKey="username" currentSort={filters.sort} onSort={handleSort} />
                    <SortableHeader label="Active" sortKey="active" currentSort={filters.sort} onSort={handleSort} />
                    <SortableHeader label="Email" sortKey="email" currentSort={filters.sort} onSort={handleSort} />
                  </tr>
                </thead>
                <tbody>
                  {accounts.map((a) => (
                    <tr
                      key={a.pid ?? a.username}
                      className="border-b last:border-0 hover:bg-muted/50 cursor-pointer"
                      onClick={() => openEdit(a.pid)}
                    >
                      <td className="py-3 px-2 font-mono text-xs">{a.pid ?? "—"}</td>
                      <td className="py-3 px-2 font-medium">{a.username}</td>
                      <td className="py-3 px-2 text-sm">{a.active ? "Yes" : "No"}</td>
                      <td className="py-3 px-2 text-sm truncate max-w-xs">{a.primaryEmail?.address ?? "—"}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}

          <div className="flex items-center justify-between pt-2">
            <div className="text-xs text-muted-foreground">
              Page {(filters.page ?? 0) + 1} / {Math.max(pagination.totalPages, 1)} · {pagination.totalItems} items
            </div>
            <div className="flex gap-2">
              <Button variant="outline" disabled={!canPrev} onClick={() => setFilters({ ...filters, page: (filters.page ?? 0) - 1 })}>
                Prev
              </Button>
              <Button variant="outline" disabled={!canNext} onClick={() => setFilters({ ...filters, page: (filters.page ?? 0) + 1 })}>
                Next
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>

      <PlayerDetailsDialog
        config={config}
        pid={selectedPid}
        open={detailsOpen}
        onOpenChange={handleDetailsClose}
        onAccountChanged={() => void loadAccounts()}
      />

      <CreatePlayerDialog
        config={config}
        open={createOpen}
        onOpenChange={setCreateOpen}
        onCreated={() => void loadAccounts()}
      />

      <EditPlayerDialog
        config={config}
        open={editOpen}
        onOpenChange={setEditOpen}
        pid={selectedPid}
        onUpdated={() => void loadAccounts()}
      />
    </div>
  )
}

