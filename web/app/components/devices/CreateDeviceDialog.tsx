import { useState } from "react";
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

import type { AppConfig } from "~/hooks/useAppConfig";
import type { CreateDeviceRequest } from "~/lib/devices";
import { createDevice } from "~/lib/devices";
import { ApiError, ManagementError } from "~/lib/api-client";

export function CreateDeviceDialog({
  config,
  open,
  onOpenChange,
  onCreated,
}: {
  config: AppConfig;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onCreated?: () => void;
}) {
  const [serialNumber, setSerialNumber] = useState("");
  const [language, setLanguage] = useState("en");
  const [platform, setPlatform] = useState("WIIU");
  const [region, setRegion] = useState("USA");
  const [systemVersion, setSystemVersion] = useState("");
  const [type, setType] = useState("RETAIL");

  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleCreate = async () => {
    if (!serialNumber.trim()) return alert("Serial number is required");
    if (!systemVersion.trim()) return alert("System version is required");

    const payload: CreateDeviceRequest = {
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
      await createDevice(config, payload);
      onOpenChange(false);
      onCreated?.();
      setSerialNumber("");
      setSystemVersion("");
    } catch (e) {
      if (e instanceof ApiError) {
        switch (e.code) {
          case ManagementError.BAD_REQUEST:
            setError(e.message || "Invalid input. Please check your data.");
            break;
          case ManagementError.CONFLICT:
            setError(e.message || "Device with this serial number already exists");
            break;
          default:
            setError(e.message || "Error creating device");
        }
      } else {
        setError(e instanceof Error ? e.message : "Error creating device");
      }
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-3xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Create Device</DialogTitle>
          <DialogDescription>Register a new device (console) in the system.</DialogDescription>
        </DialogHeader>

        {error ? <div className="text-sm text-destructive">{error}</div> : null}

        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          <div className="space-y-2 md:col-span-2">
            <Label>Serial Number</Label>
            <Input value={serialNumber} onChange={(e) => setSerialNumber(e.target.value)} placeholder="ABC123456789" />
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
            <Input value={systemVersion} onChange={(e) => setSystemVersion(e.target.value)} placeholder="5.5.5" />
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
        </div>

        <div className="flex justify-end gap-2">
          <Button variant="outline" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <Button onClick={() => void handleCreate()} disabled={saving}>
            {saving ? "Creating..." : "Create"}
          </Button>
        </div>
      </DialogContent>
    </Dialog>
  );
}
