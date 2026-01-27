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
import type { CreateAccountRequest } from "~/types/account";
import { createAccount } from "~/lib/accounts";

import countriesLanguages from "~/data/countries_languages.json";
import timezones from "~/data/timezones.json";
import regionsData from "~/data/regions_en.json";

const DEFAULT_MII_DATA = "AA==";
const DEFAULT_MII_NAME = "Player";

export function CreatePlayerDialog({
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
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [email, setEmail] = useState("");
  const [gender, setGender] = useState("male");
  const [country, setCountry] = useState("US");
  const [region, setRegion] = useState("");
  const [timezone, setTimezone] = useState("America/New_York");
  const [language, setLanguage] = useState("en");
  const [marketing, setMarketing] = useState(false);
  const [offDevice, setOffDevice] = useState(false);

  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

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
    if (!open) {
      setError(null);
      setSaving(false);
    }
  }, [open]);

  useEffect(() => {
    // Auto-select first region when country changes
    if (availableRegions.length > 0 && !region) {
      setRegion(String(availableRegions[0].id));
    }
  }, [availableRegions, region]);

  const handleCreate = async () => {
    if (!username.trim()) return alert("Username is required");
    if (!password) return alert("Password is required");
    if (!email.trim()) return alert("Email is required");
    if (!region) return alert("Region is required");

    const payload: CreateAccountRequest = {
      username: username.trim(),
      password,
      gender,
      region: parseInt(region, 10),
      timezone: timezone.trim(),
      language: language.trim(),
      country: country.trim(),
      marketing,
      offDevice,
      email: { address: email.trim() },
      mii: { name: DEFAULT_MII_NAME, data: DEFAULT_MII_DATA },
    };

    try {
      setSaving(true);
      setError(null);
      await createAccount(config, payload);
      onOpenChange(false);
      onCreated?.();
      setUsername("");
      setPassword("");
      setEmail("");
    } catch (e) {
      setError(e instanceof Error ? e.message : "Error creating user");
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-3xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Create User</DialogTitle>
          <DialogDescription>Create a new account in the Account Server.</DialogDescription>
        </DialogHeader>

        {error ? <div className="text-sm text-destructive">{error}</div> : null}

        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          <div className="space-y-2">
            <Label>Username</Label>
            <Input value={username} onChange={(e) => setUsername(e.target.value)} />
          </div>

          <div className="space-y-2">
            <Label>Password</Label>
            <Input type="password" value={password} onChange={(e) => setPassword(e.target.value)} />
          </div>

          <div className="space-y-2 md:col-span-2">
            <Label>Email</Label>
            <Input value={email} onChange={(e) => setEmail(e.target.value)} placeholder="user@example.com" />
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
