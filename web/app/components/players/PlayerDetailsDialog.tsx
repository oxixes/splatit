import { useEffect, useMemo, useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "~/components/ui/dialog";
import { Button } from "~/components/ui/button";
import { Input } from "~/components/ui/input";
import { Label } from "~/components/ui/label";
import { Switch } from "~/components/ui/switch";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "~/components/ui/tabs";
import { Card, CardContent, CardHeader, CardTitle } from "~/components/ui/card";

import type { AppConfig } from "~/hooks/useAppConfig";
import type { Account, DeviceAttribute, AccountOwnership } from "~/types/account";
import {
  getAccount,
  linkDeviceToAccount,
  listAccountDeviceAttributes,
  removeAccountDeviceAttribute,
  setAccountDeviceAttribute,
  unlinkDeviceFromAccount,
  updateAccountDeviceStatus,
} from "~/lib/accounts";

function safeNumber(v: string): number | null {
  const n = Number(v);
  if (!Number.isFinite(n)) return null;
  return n;
}

export function PlayerDetailsDialog({
  config,
  pid,
  open,
  onOpenChange,
  onAccountChanged,
}: {
  config: AppConfig;
  pid: number | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onAccountChanged?: () => void;
}) {
  const [account, setAccount] = useState<Account | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [linkDeviceId, setLinkDeviceId] = useState("");
  const [linkDeviceStatus, setLinkDeviceStatus] = useState<"active" | "inactive">("active");

  const [attributesByDeviceId, setAttributesByDeviceId] = useState<Record<number, DeviceAttribute[]>>({});
  const [attrName, setAttrName] = useState("");
  const [attrValue, setAttrValue] = useState("");
  const [selectedDeviceIdForAttr, setSelectedDeviceIdForAttr] = useState<number | null>(null);

  const ownedDevices: AccountOwnership[] = useMemo(() => account?.ownedDevices ?? [], [account]);

  useEffect(() => {
    if (!open || pid == null) return;

    const load = async () => {
      try {
        setLoading(true);
        setError(null);
        setAccount(null);

        const res = await getAccount(config, pid);
        setAccount(res.account);
      } catch (e) {
        setError(e instanceof Error ? e.message : "Error loading account");
      } finally {
        setLoading(false);
      }
    };

    void load();
  }, [open, pid, config]);

  const refreshAccount = async () => {
    if (pid == null) return;
    const res = await getAccount(config, pid);
    setAccount(res.account);
    onAccountChanged?.();
  };

  const handleUnlink = async (deviceId: number) => {
    if (pid == null) return;
    if (!confirm(`Unlink device ${deviceId} from user ${pid}?`)) return;

    await unlinkDeviceFromAccount(config, pid, deviceId);
    await refreshAccount();
  };

  const handleToggleStatus = async (deviceId: number, currentStatus: number) => {
    if (pid == null) return;
    const newStatus: "active" | "inactive" = currentStatus === 0 ? "inactive" : "active";
    await updateAccountDeviceStatus(config, pid, deviceId, newStatus);
    await refreshAccount();
  };

  const handleLoadAttributes = async (deviceId: number) => {
    if (pid == null) return;
    const res = await listAccountDeviceAttributes(config, pid, deviceId);
    setAttributesByDeviceId((prev) => ({ ...prev, [deviceId]: res.attributes }));
    setSelectedDeviceIdForAttr(deviceId);
  };

  const handleSetAttribute = async () => {
    if (pid == null) return;
    if (selectedDeviceIdForAttr == null) {
      alert("Select a device first");
      return;
    }
    if (!attrName.trim()) {
      alert("Attribute name is required");
      return;
    }

    await setAccountDeviceAttribute(config, pid, selectedDeviceIdForAttr, attrName.trim(), { value: attrValue });
    await handleLoadAttributes(selectedDeviceIdForAttr);
    await refreshAccount();
  };

  const handleRemoveAttribute = async (deviceId: number, name: string) => {
    if (pid == null) return;
    await removeAccountDeviceAttribute(config, pid, deviceId, name);
    await handleLoadAttributes(deviceId);
    await refreshAccount();
  };

  const handleLinkDevice = async () => {
    if (pid == null) return;
    const n = safeNumber(linkDeviceId);
    if (n == null) {
      alert("Invalid deviceId");
      return;
    }

    await linkDeviceToAccount(config, pid, { deviceId: n, status: linkDeviceStatus });
    setLinkDeviceId("");
    await refreshAccount();
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-4xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Manage Player Devices</DialogTitle>
          <DialogDescription>
            {pid != null ? `PID ${pid}` : ""}
            {loading ? " · Loading..." : ""}
          </DialogDescription>
        </DialogHeader>

        {error ? <div className="text-sm text-destructive">{error}</div> : null}

        {!loading && account ? (
          <div className="space-y-4">

            <Tabs defaultValue="devices">
              <TabsList>
                <TabsTrigger value="devices">Linked Devices</TabsTrigger>
                <TabsTrigger value="link">Link Device</TabsTrigger>
                <TabsTrigger value="attributes">Attributes</TabsTrigger>
              </TabsList>

              <TabsContent value="devices" className="space-y-3">
                {ownedDevices.length === 0 ? (
                  <div className="text-sm text-muted-foreground">No linked devices.</div>
                ) : (
                  ownedDevices.map((od) => (
                    <Card key={od.device.id}>
                      <CardHeader className="flex flex-row items-center justify-between">
                        <CardTitle className="text-base">Device {od.device.id}</CardTitle>
                        <div className="flex items-center gap-2">
                          <div className="flex items-center gap-2">
                            <span className="text-xs text-muted-foreground">Active</span>
                            <Switch
                              checked={od.status === 0}
                              onCheckedChange={() => void handleToggleStatus(od.device.id, od.status)}
                            />
                          </div>
                          <Button variant="destructive" onClick={() => void handleUnlink(od.device.id)}>
                            Unlink
                          </Button>
                        </div>
                      </CardHeader>
                      <CardContent className="grid grid-cols-1 md:grid-cols-2 gap-3 text-sm">
                        <div>
                          <div className="text-muted-foreground">Serial</div>
                          <div className="font-mono text-xs break-all">{od.device.serialNumber}</div>
                        </div>
                        <div>
                          <div className="text-muted-foreground">Platform</div>
                          <div className="font-medium">{od.device.platform}</div>
                        </div>
                        <div>
                          <div className="text-muted-foreground">Region</div>
                          <div className="font-medium">{od.device.region}</div>
                        </div>
                        <div>
                          <div className="text-muted-foreground">Banned</div>
                          <div className="font-medium">{od.device.banned ? "Yes" : "No"}</div>
                        </div>
                        <div className="md:col-span-2">
                          <div className="text-muted-foreground">Attributes (snapshot)</div>
                          {od.attributes?.length ? (
                            <div className="mt-1 text-xs font-mono space-y-1">
                              {od.attributes.map((a) => (
                                <div key={a.name} className="flex items-center justify-between gap-2">
                                  <span className="break-all">
                                    {a.name} = {a.value}
                                  </span>
                                </div>
                              ))}
                            </div>
                          ) : (
                            <div className="text-xs text-muted-foreground">No attributes</div>
                          )}
                        </div>
                      </CardContent>
                    </Card>
                  ))
                )}
              </TabsContent>

              <TabsContent value="link" className="space-y-3">
                <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                  <div className="space-y-2">
                    <Label>deviceId</Label>
                    <Input value={linkDeviceId} onChange={(e) => setLinkDeviceId(e.target.value)} placeholder="123" />
                  </div>
                  <div className="space-y-2">
                    <Label>Status</Label>
                    <div className="flex gap-2">
                      <Button
                        type="button"
                        variant={linkDeviceStatus === "active" ? "default" : "outline"}
                        onClick={() => setLinkDeviceStatus("active")}
                      >
                        Active
                      </Button>
                      <Button
                        type="button"
                        variant={linkDeviceStatus === "inactive" ? "default" : "outline"}
                        onClick={() => setLinkDeviceStatus("inactive")}
                      >
                        Inactive
                      </Button>
                    </div>
                  </div>
                </div>
                <Button onClick={() => void handleLinkDevice()}>Link Device</Button>
              </TabsContent>

              <TabsContent value="attributes" className="space-y-3">
                <div className="space-y-2">
                  <Label>Device</Label>
                  <div className="flex flex-wrap gap-2">
                    {ownedDevices.length === 0 ? (
                      <div className="text-sm text-muted-foreground">No devices.</div>
                    ) : (
                      ownedDevices.map((od) => (
                        <Button
                          key={od.device.id}
                          variant={selectedDeviceIdForAttr === od.device.id ? "default" : "outline"}
                          onClick={() => void handleLoadAttributes(od.device.id)}
                        >
                          {od.device.id}
                        </Button>
                      ))
                    )}
                  </div>
                </div>

                {selectedDeviceIdForAttr != null ? (
                  <Card>
                    <CardHeader>
                      <CardTitle className="text-base">Device {selectedDeviceIdForAttr} Attributes</CardTitle>
                    </CardHeader>
                    <CardContent className="space-y-3">
                      <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                        <div className="space-y-2">
                          <Label>Name</Label>
                          <Input value={attrName} onChange={(e) => setAttrName(e.target.value)} placeholder="name" />
                        </div>
                        <div className="space-y-2">
                          <Label>Value</Label>
                          <Input value={attrValue} onChange={(e) => setAttrValue(e.target.value)} placeholder="value" />
                        </div>
                      </div>
                      <Button onClick={() => void handleSetAttribute()}>Save Attribute</Button>

                      <div className="space-y-2">
                        <div className="text-sm text-muted-foreground">List</div>
                        {(attributesByDeviceId[selectedDeviceIdForAttr] ?? []).length === 0 ? (
                          <div className="text-sm text-muted-foreground">No attributes.</div>
                        ) : (
                          <div className="text-xs font-mono space-y-1">
                            {(attributesByDeviceId[selectedDeviceIdForAttr] ?? []).map((a) => (
                              <div key={a.name} className="flex items-center justify-between gap-2 border-b py-1">
                                <span className="break-all">
                                  {a.name} = {a.value}
                                </span>
                                <Button
                                  variant="destructive"
                                  size="sm"
                                  onClick={() => void handleRemoveAttribute(selectedDeviceIdForAttr, a.name)}
                                >
                                  Remove
                                </Button>
                              </div>
                            ))}
                          </div>
                        )}
                      </div>
                    </CardContent>
                  </Card>
                ) : (
                  <div className="text-sm text-muted-foreground">Select a device to view/edit attributes.</div>
                )}
              </TabsContent>
            </Tabs>
          </div>
        ) : null}
      </DialogContent>
    </Dialog>
  );
}
