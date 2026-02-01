import type { Route } from "./+types/devices";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "~/components/ui/card";
import { Smartphone, Pencil } from "lucide-react";
import { Button } from "~/components/ui/button";
import { Input } from "~/components/ui/input";
import { Label } from "~/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "~/components/ui/select";
import { useEffect, useMemo, useState } from "react";
import { useAppConfig } from "~/hooks/useAppConfig";
import type { Device, ListDevicesFilters } from "~/lib/devices";
import { listDevices } from "~/lib/devices";
import { CreateDeviceDialog } from "~/components/devices/CreateDeviceDialog";
import { EditDeviceDialog } from "~/components/devices/EditDeviceDialog";
import { SortableHeader } from "~/components/ui/sortable-header";
import { ApiError, ManagementError } from "~/lib/api-client";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Devices - SplatIt Server" },
    { name: "description", content: "Manage registered devices." },
  ];
}

const PLATFORM_NAMES: Record<string, string> = {
  "WIIU": "Wii U",
};

const REGION_NAMES: Record<string, string> = {
  "JPN": "Japan",
  "USA": "USA",
  "EUR": "Europe",
  "AUS": "Australia",
  "CHN": "China",
  "KOR": "Korea",
  "TWN": "Taiwan",
};

export default function Devices() {
  const { config } = useAppConfig();

  const [devices, setDevices] = useState<Device[]>([]);
  const [pagination, setPagination] = useState<{ totalItems: number; totalPages: number; currentPage: number }>({
    totalItems: 0,
    totalPages: 0,
    currentPage: 0,
  });
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [filters, setFilters] = useState<ListDevicesFilters>({
    page: 0,
    pageSize: 25,
    sort: "id_desc",
  });

  const [searchSerial, setSearchSerial] = useState("");

  const [createOpen, setCreateOpen] = useState(false);
  const [editOpen, setEditOpen] = useState(false);
  const [selectedDeviceId, setSelectedDeviceId] = useState<number | null>(null);

  const loadDevices = async () => {
    try {
      setLoading(true);
      setError(null);
      const res = await listDevices(config, filters);
      setDevices(res.devices);
      setPagination(res.pagination);
    } catch (e) {
      if (e instanceof ApiError) {
        setError(e.message || "Error loading devices");
      } else {
        setError(e instanceof Error ? e.message : "Error loading devices");
      }
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void loadDevices();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [filters]);

  const openEdit = (deviceId: number) => {
    setSelectedDeviceId(deviceId);
    setEditOpen(true);
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
          <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">Devices</h1>
          <p className="text-muted-foreground mt-2">View and manage registered devices (consoles)</p>
        </div>
        <Button onClick={() => setCreateOpen(true)}>Create Device</Button>
      </div>

      <Card>
        <CardHeader>
          <div className="flex flex-col md:flex-row md:items-end md:justify-between gap-3">
            <div>
              <CardTitle>Device List</CardTitle>
              <CardDescription>All registered devices in the system</CardDescription>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-2">
              <div className="space-y-1">
                <Label>Serial Number</Label>
                <Input value={searchSerial} onChange={(e) => setSearchSerial(e.target.value)} placeholder="Search..." />
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
                  onClick={() =>
                    setFilters({ ...filters, serialNumber: searchSerial.trim() || undefined, page: 0 })
                  }
                >
                  Apply
                </Button>
                <Button
                  variant="outline"
                  onClick={() => {
                    setSearchSerial("");
                    setFilters({ ...filters, serialNumber: undefined, page: 0 });
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
            <div className="text-sm text-muted-foreground py-8">Loading devices...</div>
          ) : devices.length === 0 ? (
            <div className="text-center py-12 text-muted-foreground">
              <Smartphone className="h-12 w-12 mx-auto mb-4 opacity-50" />
              <p className="text-lg font-medium">No devices found</p>
              <p className="text-sm mt-2">Try adjusting filters or create a device.</p>
            </div>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead>
                  <tr className="border-b">
                    <SortableHeader label="ID" sortKey="id" currentSort={filters.sort} onSort={handleSort} />
                    <SortableHeader label="Serial Number" sortKey="serial" currentSort={filters.sort} onSort={handleSort} />
                    <th className="text-left py-2 px-2 text-sm font-medium">Platform</th>
                    <th className="text-left py-2 px-2 text-sm font-medium">Region</th>
                    <th className="text-left py-2 px-2 text-sm font-medium">System Ver.</th>
                    <th className="text-left py-2 px-2 text-sm font-medium">Banned</th>
                    <th className="text-left py-2 px-2 text-sm font-medium">Actions</th>
                  </tr>
                </thead>
                <tbody>
                  {devices.map((d) => (
                    <tr key={d.id} className="border-b last:border-0 hover:bg-muted/50 cursor-pointer" onClick={() => openEdit(d.id)}>
                      <td className="py-3 px-2 font-mono text-xs">{d.id}</td>
                      <td className="py-3 px-2 font-mono text-xs">{d.serialNumber}</td>
                      <td className="py-3 px-2 text-sm">{PLATFORM_NAMES[d.platform] ?? d.platform}</td>
                      <td className="py-3 px-2 text-sm">{REGION_NAMES[d.region] ?? d.region}</td>
                      <td className="py-3 px-2 text-sm">{d.systemVersion}</td>
                      <td className="py-3 px-2 text-sm">{d.banned ? "Yes" : "No"}</td>
                      <td className="py-3 px-2">
                        <Button
                          variant="ghost"
                          size="sm"
                          onClick={(e) => {
                            e.stopPropagation();
                            openEdit(d.id);
                          }}
                          title="Edit device"
                        >
                          <Pencil className="h-4 w-4" />
                        </Button>
                      </td>
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
              <Button
                variant="outline"
                disabled={!canPrev}
                onClick={() => setFilters({ ...filters, page: (filters.page ?? 0) - 1 })}
              >
                Prev
              </Button>
              <Button
                variant="outline"
                disabled={!canNext}
                onClick={() => setFilters({ ...filters, page: (filters.page ?? 0) + 1 })}
              >
                Next
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>

      <CreateDeviceDialog
        config={config}
        open={createOpen}
        onOpenChange={setCreateOpen}
        onCreated={() => void loadDevices()}
      />

      <EditDeviceDialog
        config={config}
        open={editOpen}
        onOpenChange={setEditOpen}
        deviceId={selectedDeviceId}
        onUpdated={() => void loadDevices()}
      />
    </div>
  );
}
