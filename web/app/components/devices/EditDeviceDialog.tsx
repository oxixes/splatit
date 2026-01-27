import { useState, useEffect } from "react";
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
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "~/components/ui/select";
import { Checkbox } from "~/components/ui/checkbox";

import type { AppConfig } from "~/hooks/useAppConfig";
import { getDevice, updateDevice, deleteDevice, banDevice, unbanDevice } from "~/lib/devices";
import type { Device } from "~/lib/devices";


export function EditDeviceDialog({
  config,
  open,
  onOpenChange,
  deviceId,
  onUpdated,
}: {
  config: AppConfig;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  deviceId: number | null;
  onUpdated?: () => void;
}) {
  const [device, setDevice] = useState<Device | null>(null);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [serialNumber, setSerialNumber] = useState("");
  const [language, setLanguage] = useState("en");
  const [platform, setPlatform] = useState("WIIU");
  const [region, setRegion] = useState("USA");
  const [systemVersion, setSystemVersion] = useState("");
  const [type, setType] = useState("RETAIL");
  const [banned, setBanned] = useState(false);

  useEffect(() => {
    if (!open || deviceId == null) return;

    const load = async () => {
      try {
        setLoading(true);
        setError(null);
        const res = await getDevice(config, deviceId);
        const dev = res.device;
        setDevice(dev);
        setSerialNumber(dev.serialNumber);
        setLanguage(dev.language);
        setPlatform(dev.platform);
        setRegion(dev.region);
        setSystemVersion(dev.systemVersion);
        setType(dev.type);
        setBanned(dev.banned);
      } catch (e) {
        setError(e instanceof Error ? e.message : "Error loading device");
      } finally {
        setLoading(false);
      }
    };

    void load();
  }, [open, deviceId, config]);

  const handleUpdate = async () => {
    if (deviceId == null) return;
    if (!serialNumber.trim()) return alert("Serial number is required");
    if (!systemVersion.trim()) return alert("System version is required");

    const payload = {
      serialNumber: serialNumber.trim(),
      language: language.trim(),
      platform: platform,
      region: region,
      systemVersion: systemVersion.trim(),
      type: type.trim(),
    };

    try {
      setSaving(true);
      setError(null);
      await updateDevice(config, deviceId, payload);
      onOpenChange(false);
      onUpdated?.();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Error updating device");
    } finally {
      setSaving(false);
    }
  };

  const handleDelete = async () => {
    if (deviceId == null) return;
    if (!confirm(`Are you sure you want to delete device ${serialNumber} (ID: ${deviceId})? This action cannot be undone.`)) return;

    try {
      setSaving(true);
      await deleteDevice(config, deviceId);
      onOpenChange(false);
      onUpdated?.();
    } catch (e) {
      alert(e instanceof Error ? e.message : "Error deleting device");
    } finally {
      setSaving(false);
    }
  };

  const handleBanToggle = async () => {
    if (deviceId == null) return;

    try {
      setSaving(true);
      if (banned) {
        await unbanDevice(config, deviceId);
      } else {
        if (!confirm(`Are you sure you want to ban device ${serialNumber}?`)) return;
        await banDevice(config, deviceId);
      }
      // Reload device data
      const res = await getDevice(config, deviceId);
      setBanned(res.device.banned);
      setDevice(res.device);
    } catch (e) {
      alert(e instanceof Error ? e.message : `Error ${banned ? "unbanning" : "banning"} device`);
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-3xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Edit Device</DialogTitle>
          <DialogDescription>
            {deviceId != null ? `ID ${deviceId}` : ""}
            {loading ? " · Loading..." : ""}
            {device?.lastUpdated ? ` · Last Updated: ${new Date(device.lastUpdated * 1000).toLocaleString()}` : ""}
          </DialogDescription>
        </DialogHeader>

        {error ? <div className="text-sm text-destructive">{error}</div> : null}

        {!loading && device ? (
          <div className="space-y-4">
            <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
              <div className="space-y-2 md:col-span-2">
                <Label>Serial Number</Label>
                <Input value={serialNumber} onChange={(e) => setSerialNumber(e.target.value)} />
              </div>

              <div className="space-y-2">
                <Label>Language</Label>
                <Select value={language} onValueChange={setLanguage}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="en">English</SelectItem>
                    <SelectItem value="es">Spanish</SelectItem>
                    <SelectItem value="fr">French</SelectItem>
                    <SelectItem value="de">German</SelectItem>
                    <SelectItem value="it">Italian</SelectItem>
                    <SelectItem value="nl">Dutch</SelectItem>
                    <SelectItem value="pt">Portuguese</SelectItem>
                    <SelectItem value="ru">Russian</SelectItem>
                    <SelectItem value="ja">Japanese</SelectItem>
                    <SelectItem value="ko">Korean</SelectItem>
                    <SelectItem value="zh">Chinese</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>Platform</Label>
                <Select value={platform} onValueChange={setPlatform}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="WIIU">Wii U</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>Region</Label>
                <Select value={region} onValueChange={setRegion}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="JPN">Japan</SelectItem>
                    <SelectItem value="USA">USA</SelectItem>
                    <SelectItem value="EUR">Europe</SelectItem>
                    <SelectItem value="AUS">Australia</SelectItem>
                    <SelectItem value="CHN">China</SelectItem>
                    <SelectItem value="KOR">Korea</SelectItem>
                    <SelectItem value="TWN">Taiwan</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>System Version</Label>
                <Input value={systemVersion} onChange={(e) => setSystemVersion(e.target.value)} />
              </div>

              <div className="space-y-2">
                <Label>Type</Label>
                <Select value={type} onValueChange={setType}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="RETAIL">Retail</SelectItem>
                    <SelectItem value="EMULATED">Emulated</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2 md:col-span-2">
                <div className="flex items-center space-x-2">
                  <Checkbox id="banned" checked={banned} onCheckedChange={(checked) => void handleBanToggle()} />
                  <Label htmlFor="banned" className="cursor-pointer font-semibold">Banned (⚠️ Banning will prevent device usage)</Label>
                </div>
              </div>
            </div>

            <div className="flex justify-between gap-2">
              <Button variant="destructive" onClick={() => void handleDelete()} disabled={saving}>
                Delete Device
              </Button>
              <div className="flex gap-2">
                <Button variant="outline" onClick={() => onOpenChange(false)}>
                  Cancel
                </Button>
                <Button onClick={() => void handleUpdate()} disabled={saving}>
                  {saving ? "Saving..." : "Save Changes"}
                </Button>
              </div>
            </div>
          </div>
        ) : null}
      </DialogContent>
    </Dialog>
  );
}
