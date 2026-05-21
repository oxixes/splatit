import type { Route } from "./+types/map-rotation";
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Button} from "~/components/ui/button";
import {Label} from "~/components/ui/label";
import {Input} from "~/components/ui/input";
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from "~/components/ui/select";
import {Link} from "react-router";
import {AlertCircle, RefreshCw, Save, Shuffle, Plus, Trash2, ChevronUp, ChevronDown} from "lucide-react";
import {useState, useEffect} from "react";
import {useAppConfig} from "~/hooks/useAppConfig";
import {getMapRotation, updateMapRotation, randomizeMapRotation} from "~/lib/map-rotation";
import type {PhaseData} from "~/lib/map-rotation";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Map Rotation Editor - SplatIt Server" },
    { name: "description", content: "Edit the map rotation schedule." },
  ];
}

const STAGES: Record<number, string> = {
  0: "Urchin Underpass",
  1: "Walleye Warehouse",
  2: "Saltspray Rig",
  3: "Arowana Mall",
  4: "Blackbelly Skatepark",
  5: "Camp Triggerfish",
  6: "Port Mackerel",
  7: "Kelp Dome",
  8: "Moray Towers",
  9: "Bluefin Depot",
  10: "Hammerhead Bridge",
  11: "Flounder Heights",
  12: "Museum d'Alfonsino",
  13: "Ancho-V Games",
  14: "Piranha Pit",
  15: "Mahi-Mahi Resort",
};

const RULES: Record<string, string> = {
  "cPnt": "Turf War",
  "cVar": "Splat Zones",
  "cVlf": "Tower Control",
  "cVgl": "Rainmaker",
};

function createEmptyPhase(): PhaseData {
  return {
    gachiRule: "cVar",
    regularRule: "cPnt",
    gachiStages: [0, 0],
    regularStages: [0, 0],
    duration: 4,
  };
}

export default function MapRotationEditor() {
  const { config } = useAppConfig();
  const [phases, setPhases] = useState<PhaseData[]>([]);
  const [afterFesBonusStart, setAfterFesBonusStart] = useState<string>("");
  const [isLoading, setIsLoading] = useState(true);
  const [isSaving, setIsSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);

  useEffect(() => {
    loadRotation();
  }, []);

  const loadRotation = async () => {
    try {
      setIsLoading(true);
      const data = await getMapRotation(config);
      setPhases(data.rotation.phases || []);
      setAfterFesBonusStart(data.afterFesBonusStart || "");
    } catch (e) {
      setError("Failed to load map rotation");
      console.error(e);
    } finally {
      setIsLoading(false);
    }
  };

  const handleSave = async () => {
    try {
      setIsSaving(true);
      setMessage(null);
      await updateMapRotation(config, { phases });
      setMessage("Map rotation saved successfully!");
    } catch (e) {
      setError("Failed to save map rotation");
      console.error(e);
    } finally {
      setIsSaving(false);
    }
  };

  const handleRandomize = async () => {
    try {
      setIsSaving(true);
      setMessage(null);
      const data = await randomizeMapRotation(config);
      setPhases(data.rotation.phases || []);
      setMessage("Map rotation randomized successfully!");
    } catch (e) {
      setError("Failed to randomize map rotation");
      console.error(e);
    } finally {
      setIsSaving(false);
    }
  };

  const updatePhase = (index: number, updates: Partial<PhaseData>) => {
    setPhases(prev => prev.map((p, i) => i === index ? { ...p, ...updates } : p));
  };

  const addPhase = () => {
    setPhases(prev => [...prev, createEmptyPhase()]);
  };

  const removePhase = (index: number) => {
    setPhases(prev => prev.filter((_, i) => i !== index));
  };

  const movePhase = (index: number, direction: -1 | 1) => {
    setPhases(prev => {
      const newPhases = [...prev];
      const target = index + direction;
      if (target < 0 || target >= newPhases.length) return prev;
      [newPhases[index], newPhases[target]] = [newPhases[target], newPhases[index]];
      return newPhases;
    });
  };

  if (isLoading) {
    return (
      <div className="space-y-6">
        <Link to="/settings" className="text-sm text-muted-foreground hover:text-foreground">
          ← Back to Settings
        </Link>
        <div className="text-center py-12 text-muted-foreground">Loading map rotation...</div>
      </div>
    );
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <Link to="/settings" className="text-sm text-muted-foreground hover:text-foreground">
            ← Back to Settings
          </Link>
          <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight mt-2">
            Map Rotation Editor
          </h1>
          <p className="text-muted-foreground mt-2">
            Manage the hourly map rotation schedule. {phases.length} phases configured.
          </p>
        </div>
        <div className="flex gap-2">
          <Button variant="outline" onClick={handleRandomize} disabled={isSaving}>
            <Shuffle className="mr-2 h-4 w-4" />
            Randomize All
          </Button>
          <Button onClick={handleSave} disabled={isSaving}>
            <Save className="mr-2 h-4 w-4" />
            {isSaving ? "Saving..." : "Save Rotation"}
          </Button>
        </div>
      </div>

      {error && (
        <Card className="border-red-500/50 bg-red-500/5">
          <CardContent className="pt-4">
            <p className="text-red-500 text-sm">{error}</p>
          </CardContent>
        </Card>
      )}

      {message && (
        <Card className="border-green-500/50 bg-green-500/5">
          <CardContent className="pt-4">
            <p className="text-green-500 text-sm">{message}</p>
          </CardContent>
        </Card>
      )}

      <Card>
        <CardHeader>
          <CardTitle>After-Fes Bonus Start</CardTitle>
          <CardDescription>
            This value comes from the active festival and cannot be edited here.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            <Label>After-Fes Bonus Start (from active festival)</Label>
            <Input
              type="text"
              value={afterFesBonusStart || "N/A"}
              disabled
              className="bg-muted"
            />
            <p className="text-xs text-muted-foreground">
              Edit this value in the active festival&apos;s configuration.
            </p>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Map Rotation Phases</CardTitle>
          <CardDescription>
            Each phase defines map and rule combinations for one rotation period.
            The last phase should have a long duration (e.g., 87600 = 10 years).
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="flex justify-between items-center">
            <p className="text-sm text-muted-foreground">{phases.length} phases</p>
            <Button variant="outline" size="sm" onClick={addPhase}>
              <Plus className="mr-2 h-3 w-3" />
              Add Phase
            </Button>
          </div>

          <div className="space-y-2 max-h-[70vh] overflow-y-auto">
            {phases.map((phase, index) => (
              <Card key={index} className="bg-muted/30">
                <CardContent className="pt-4 space-y-3">
                  <div className="flex items-center justify-between">
                    <Label className="font-medium">Phase {index + 1}{index === phases.length - 1 ? " (Last - 10yr)" : ""}</Label>
                    <div className="flex items-center gap-1">
                      <Button variant="ghost" size="sm" onClick={() => movePhase(index, -1)} disabled={index === 0}>
                        <ChevronUp className="h-3 w-3" />
                      </Button>
                      <Button variant="ghost" size="sm" onClick={() => movePhase(index, 1)} disabled={index === phases.length - 1}>
                        <ChevronDown className="h-3 w-3" />
                      </Button>
                      <Button variant="ghost" size="sm" onClick={() => removePhase(index)} disabled={phases.length <= 1}>
                        <Trash2 className="h-3 w-3 text-destructive" />
                      </Button>
                    </div>
                  </div>

                  <div className="grid grid-cols-2 gap-3">
                    <div className="space-y-1">
                      <Label className="text-xs">Competitive Rule</Label>
                      <Select
                        value={phase.gachiRule}
                        onValueChange={(v) => updatePhase(index, { gachiRule: v })}
                      >
                        <SelectTrigger className="h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          {Object.entries(RULES).filter(([k]) => k !== "cPnt").map(([k, v]) => (
                            <SelectItem key={k} value={k}>{v}</SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>

                    <div className="space-y-1">
                      <Label className="text-xs">Duration (hours)</Label>
                      <Input
                        type="number"
                        className="h-8"
                        value={phase.duration}
                        onChange={(e) => updatePhase(index, { duration: parseInt(e.target.value) || 4 })}
                        min={1}
                      />
                    </div>
                  </div>

                  <div className="space-y-1">
                    <Label className="text-xs">Competitive Stages</Label>
                    <div className="grid grid-cols-2 gap-2">
                      {phase.gachiStages.map((s, si) => (
                        <Select
                          key={si}
                          value={s.toString()}
                          onValueChange={(v) => {
                            const stages = [...phase.gachiStages];
                            stages[si] = parseInt(v);
                            updatePhase(index, { gachiStages: stages });
                          }}
                        >
                          <SelectTrigger className="h-8">
                            <SelectValue />
                          </SelectTrigger>
                          <SelectContent>
                            {Object.entries(STAGES).map(([k, v]) => (
                              <SelectItem key={k} value={k}>{v}</SelectItem>
                            ))}
                          </SelectContent>
                        </Select>
                      ))}
                    </div>
                  </div>

                  <div className="space-y-1">
                    <Label className="text-xs">Regular Stages</Label>
                    <div className="grid grid-cols-2 gap-2">
                      {phase.regularStages.map((s, si) => (
                        <Select
                          key={si}
                          value={s.toString()}
                          onValueChange={(v) => {
                            const stages = [...phase.regularStages];
                            stages[si] = parseInt(v);
                            updatePhase(index, { regularStages: stages });
                          }}
                        >
                          <SelectTrigger className="h-8">
                            <SelectValue />
                          </SelectTrigger>
                          <SelectContent>
                            {Object.entries(STAGES).map(([k, v]) => (
                              <SelectItem key={k} value={k}>{v}</SelectItem>
                            ))}
                          </SelectContent>
                        </Select>
                      ))}
                    </div>
                  </div>
                </CardContent>
              </Card>
            ))}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
