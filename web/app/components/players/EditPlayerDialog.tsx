import { useState, useMemo, useEffect } from "react";
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
import type { UpdateAccountRequest } from "~/types/account";
import { updateAccount, deleteAccount, getAccount } from "~/lib/accounts";
import type { Account } from "~/types/account";
import { ApiError, ManagementError } from "~/lib/api-client";

import countriesLanguages from "~/data/countries_languages.json";
import timezones from "~/data/timezones.json";
import regionsData from "~/data/regions_en.json";

export function EditPlayerDialog({
  config,
  open,
  onOpenChange,
  pid,
  onUpdated,
}: {
  config: AppConfig;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  pid: number | null;
  onUpdated?: () => void;
}) {
  const [account, setAccount] = useState<Account | null>(null);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [username, setUsername] = useState("");
  const [email, setEmail] = useState("");
  const [gender, setGender] = useState("male");
  const [country, setCountry] = useState("US");
  const [region, setRegion] = useState("");
  const [timezone, setTimezone] = useState("America/New_York");
  const [language, setLanguage] = useState("en");
  const [marketing, setMarketing] = useState(false);
  const [offDevice, setOffDevice] = useState(false);
  const [active, setActive] = useState(true);
  const [isAdmin, setIsAdmin] = useState(false);
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");

  useEffect(() => {
    if (!open || pid == null) return;

    const load = async () => {
      try {
        setLoading(true);
        setError(null);
        const res = await getAccount(config, pid);
        const acc = res.account;
        setAccount(acc);
        setUsername(acc.username);
        setEmail(acc.primaryEmail?.address || "");
        setGender(acc.gender === 0 ? "male" : "female");

        // Find region country from region ID
        const regionId = acc.region;
        let foundCountry = acc.country || "US";
        let foundRegion = String(regionId);

        for (const countryData of (regionsData as any[])) {
          const regionMatch = countryData.regions.find((r: any) => r.id === regionId);
          if (regionMatch) {
            foundCountry = countryData.code;
            foundRegion = String(regionId);
            break;
          }
        }

        setCountry(foundCountry);
        setRegion(foundRegion);
        setTimezone(acc.timezone || "America/New_York");
        setLanguage(acc.language || "en");
        setMarketing(acc.marketing ?? false);
        setOffDevice(acc.offDevice ?? false);
        setActive(acc.active);
        setIsAdmin(acc.isAdmin ?? false);
      } catch (e) {
        if (e instanceof ApiError) {
          switch (e.code) {
            case ManagementError.NOT_FOUND:
              setError("Account not found");
              break;
            default:
              setError(e.message || "Error loading account");
          }
        } else {
          setError(e instanceof Error ? e.message : "Error loading account");
        }
      } finally {
        setLoading(false);
      }
    };

    void load();
  }, [open, pid, config]);

  const availableLanguages = useMemo(() => {
    const countryData = (countriesLanguages.countries as any)[country];
    if (!countryData?.languages) return [];
    return Object.keys(countryData.languages);
  }, [country]);

  const availableRegions = useMemo(() => {
    const countryData = (regionsData as any[]).find((c: any) => c.code === country);
    return countryData?.regions || [];
  }, [country]);

  useEffect(() => {
    // Auto-select first region when country changes and no region is selected
    if (availableRegions.length > 0 && !region) {
      setRegion(String(availableRegions[0].id));
    }
  }, [availableRegions, region]);

  const handleUpdate = async () => {
    if (pid == null) return;
    if (!username.trim()) return alert("Username is required");
    if (!email.trim()) return alert("Email is required");
    if (!region) return alert("Region is required");
    if (newPassword && newPassword !== confirmPassword) return alert("Passwords do not match");

    const payload: UpdateAccountRequest = {
      username: username.trim(),
      gender: gender === "male" ? "0" : "1",
      region: parseInt(region, 10),
      timezone: timezone.trim(),
      language: language.trim(),
      country: country.trim(),
      marketing,
      offDevice,
      active,
      isAdmin,
      email: { address: email.trim() },
    };

    if (newPassword) {
      payload.password = newPassword;
    }

    try {
      setSaving(true);
      setError(null);
      await updateAccount(config, pid, payload);
      setNewPassword("");
      setConfirmPassword("");
      onOpenChange(false);
      onUpdated?.();
    } catch (e) {
      if (e instanceof ApiError) {
        switch (e.code) {
          case ManagementError.NOT_FOUND:
            setError("Account not found");
            break;
          case ManagementError.BAD_REQUEST:
            setError(e.message || "Invalid input. Please check your data.");
            break;
          case ManagementError.CONFLICT:
            setError(e.message || "Username or email already exists");
            break;
          default:
            setError(e.message || "Error updating user");
        }
      } else {
        setError(e instanceof Error ? e.message : "Error updating user");
      }
    } finally {
      setSaving(false);
    }
  };

  const handleDelete = async () => {
    if (pid == null) return;
    if (!confirm(`Are you sure you want to delete user ${username} (PID: ${pid})? This action cannot be undone.`)) return;

    try {
      setSaving(true);
      await deleteAccount(config, pid);
      onOpenChange(false);
      onUpdated?.();
    } catch (e) {
      let errorMsg = "Error deleting user";
      if (e instanceof ApiError) {
        switch (e.code) {
          case ManagementError.NOT_FOUND:
            errorMsg = "Account not found";
            break;
          default:
            errorMsg = e.message || errorMsg;
        }
      } else if (e instanceof Error) {
        errorMsg = e.message;
      }
      alert(errorMsg);
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-5xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Edit Player</DialogTitle>
          <DialogDescription>
            {pid != null ? `PID ${pid}` : ""}
            {loading ? " · Loading..." : ""}
            {account?.created ? ` · Created: ${new Date(account.created * 1000).toLocaleString()}` : ""}
            {account?.updated ? ` · Updated: ${new Date(account.updated * 1000).toLocaleString()}` : ""}
          </DialogDescription>
        </DialogHeader>

        {error ? <div className="text-sm text-destructive">{error}</div> : null}

        {!loading && account ? (
          <div className="space-y-4">
            <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
              <div className="space-y-2">
                <Label>Username</Label>
                <Input value={username} onChange={(e) => setUsername(e.target.value)} />
              </div>

              <div className="space-y-2 md:col-span-2">
                <Label>Email</Label>
                <Input value={email} onChange={(e) => setEmail(e.target.value)} />
              </div>

              <div className="space-y-2">
                <Label>New Password</Label>
                <Input type="password" value={newPassword} onChange={(e) => setNewPassword(e.target.value)} placeholder="Leave blank to keep current" />
              </div>

              <div className="space-y-2">
                <Label>Confirm Password</Label>
                <Input type="password" value={confirmPassword} onChange={(e) => setConfirmPassword(e.target.value)} placeholder="Confirm new password" />
              </div>

              <div className="space-y-2">
                <Label>Gender</Label>
                <div className="flex gap-2">
                  <Button type="button" variant={gender === "male" ? "default" : "outline"} onClick={() => setGender("male")}>
                    Male
                  </Button>
                  <Button type="button" variant={gender === "female" ? "default" : "outline"} onClick={() => setGender("female")}>
                    Female
                  </Button>
                </div>
              </div>

              <div className="space-y-2">
                <Label>Country</Label>
                <Select value={country} onValueChange={(val) => {
                  setCountry(val);
                  // Region will be auto-selected by useEffect
                  setRegion("");
                  if (!availableLanguages.includes(language)) setLanguage(availableLanguages[0] || 'en');
                }}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent className="max-h-[300px]">
                    {Object.entries(countriesLanguages.countries).map(([code, data]: [string, any]) => (
                      <SelectItem key={code} value={code}>
                        {data.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>Region (within country)</Label>
                <Select value={region} onValueChange={setRegion} disabled={!country || availableRegions.length === 0}>
                  <SelectTrigger>
                    <SelectValue placeholder="Select region..." />
                  </SelectTrigger>
                  <SelectContent className="max-h-[300px]">
                    {availableRegions.map((r: any) => (
                      <SelectItem key={r.id} value={String(r.id)}>
                        {r.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>Language</Label>
                <Select value={language} onValueChange={setLanguage}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {availableLanguages.map((lang: string) => (
                      <SelectItem key={lang} value={lang}>
                        {lang.toUpperCase()}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2 md:col-span-2">
                <Label>Timezone</Label>
                <Select value={timezone} onValueChange={setTimezone}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent className="max-h-[300px]">
                    {(timezones as string[]).map((tz) => (
                      <SelectItem key={tz} value={tz}>
                        {tz}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <div className="flex items-center space-x-2">
                  <Checkbox id="marketing" checked={marketing} onCheckedChange={(checked) => setMarketing(checked as boolean)} />
                  <Label htmlFor="marketing" className="cursor-pointer">Receive marketing emails</Label>
                </div>
              </div>

              <div className="space-y-2">
                <div className="flex items-center space-x-2">
                  <Checkbox id="offdevice" checked={offDevice} onCheckedChange={(checked) => setOffDevice(checked as boolean)} />
                  <Label htmlFor="offdevice" className="cursor-pointer">Allow access off device</Label>
                </div>
              </div>

              <div className="space-y-2 md:col-span-2">
                <div className="flex items-center space-x-2">
                  <Checkbox id="active" checked={active} onCheckedChange={(checked) => setActive(checked as boolean)} />
                  <Label htmlFor="active" className="cursor-pointer font-semibold">Active (⚠️ Deactivating will ban the user)</Label>
                </div>
              </div>

              <div className="space-y-2 md:col-span-2">
                <div className="flex items-center space-x-2">
                  <Checkbox id="isAdmin" checked={isAdmin} onCheckedChange={(checked) => setIsAdmin(checked as boolean)} />
                  <Label htmlFor="isAdmin" className="cursor-pointer font-semibold">Admin (can access management UI)</Label>
                </div>
              </div>
            </div>

            <div className="flex flex-col sm:flex-row justify-between gap-3">
              <div className="flex flex-wrap gap-2">
                <Button variant="destructive" onClick={() => void handleDelete()} disabled={saving}>
                  Delete User
                </Button>
                <Button variant="outline" onClick={() => { onOpenChange(false); window.dispatchEvent(new CustomEvent('openPlayerDevices', { detail: { pid } })); }}>
                  Manage Devices
                </Button>
              </div>
              <div className="flex flex-wrap gap-2 sm:justify-end">
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
