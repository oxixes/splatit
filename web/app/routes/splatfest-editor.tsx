import type { Route } from "./+types/splatfest-editor"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Button} from "~/components/ui/button";
import {Label} from "~/components/ui/label";
import {Input} from "~/components/ui/input";
import {Textarea} from "~/components/ui/textarea";
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from "~/components/ui/select";
import {Tabs, TabsContent, TabsList, TabsTrigger} from "~/components/ui/tabs";
import {Sparkles, Plus, Trash2, Download, AlertCircle, Upload, Image as ImageIcon} from "lucide-react";
import {Link} from "react-router";
import {Separator} from "~/components/ui/separator";
import {useState, useRef} from "react";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Splatfest Editor - SplatIt Server" },
    { name: "description", content: "Create and edit Splatfest events." },
  ]
}

// Types
type DialogueLine = {
  command: string;
  emotion: string;
  speaker: string;
  text: string;
  waitButton: boolean;
};

type Color = {
  r: number;
  g: number;
  b: number;
  a: number;
};

type TeamInfo = {
  color: Color;
  names: Record<string, string>;
  shortNames: Record<string, string>;
  albedoImage: string; // base64
};

type SplatfestData = {
  id: number;
  battleResultRate: number;
  lowPopulationNotJP: boolean;
  separateMatchingJP: boolean;
  gamemode: string;
  backupLanguage: string;
  stages: number[];
  teamA: TeamInfo;
  teamB: TeamInfo;
  neutralColor: Color;
  panelImage: string; // base64
  announceTime: string;
  startTime: string;
  endTime: string;
  resultTime: string;
  afterFesBonusStart: string;
  announceNews: Record<string, DialogueLine[]>;
  startNews: Record<string, DialogueLine[]>;
  resultANews: Record<string, DialogueLine[]>;
  resultBNews: Record<string, DialogueLine[]>;
};

// Languages supported
const LANGUAGES = [
    { id: "eu_de", label: "European German", flag: "🇩🇪" },
    { id: "eu_en", label: "European English", flag: "🇬🇧" },
    { id: "eu_es", label: "European Spanish", flag: "🇪🇸" },
    { id: "eu_fr", label: "European French", flag: "🇫🇷" },
    { id: "eu_it", label: "European Italian", flag: "🇮🇹" },
    { id: "jp", label: "Japanese", flag: "🇯🇵" },
    { id: "us_en", label: "American English", flag: "🇺🇸" },
    { id: "us_es", label: "American Spanish", flag: "🇲🇽" },
    { id: "us_fr", label: "American French", flag: "🇨🇦" },
];

const STAGES = [
    { id: 0, name: "Urchin Underpass" },
    { id: 1, name: "Walleye Warehouse" },
    { id: 2, name: "Saltspray Rig" },
    { id: 3, name: "Arowana Mall" },
    { id: 4, name: "Blackbelly Skatepark" },
    { id: 6, name: "Port Mackerel" },
    { id: 7, name: "Kelp Dome" },
    { id: 9, name: "Bluefin Depot" },
    { id: 8, name: "Moray Towers" },
    { id: 5, name: "Camp Triggerfish" },
    { id: 11, name: "Flounder Heights" },
    { id: 10, name: "Hammerhead Bridge" },
    { id: 12, name: "Museum d'Alfonsino" },
    { id: 15, name: "Mahi-Mahi Resort" },
    { id: 14, name: "Piranha Pit" },
    { id: 13, name: "Ancho-V Games" },
];

const SPEAKERS = [
    { id: "callie", label: "Callie (Left)" },
    { id: "marie", label: "Marie (Right)" },
    { id: "both", label: "Both" },
];

const EMOTIONS = [
    { id: "normal", label: "Normal Talk" },
    { id: "greeting", label: "Greeting" },
    { id: "happy", label: "Happy" },
    { id: "angry", label: "Angry" },
    { id: "surprised", label: "Surprised" },
    { id: "bored", label: "Bored" },
    { id: "feed", label: "Feed" },
];

const GAMEMODES = [
    { id: "turf_war", label: "Turf War" },
    { id: "splat_zones", label: "Splat Zones" },
    { id: "tower_control", label: "Tower Control" },
    { id: "rainmaker", label: "Rainmaker" },
];

// Helper functions
const processAndResizeImage = async (file: File, targetWidth: number, targetHeight: number): Promise<string> => {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      try {
        // Create canvas with target dimensions
        const canvas = document.createElement('canvas');
        canvas.width = targetWidth;
        canvas.height = targetHeight;
        const ctx = canvas.getContext('2d');

        if (!ctx) {
          reject(new Error('Could not get canvas context'));
          return;
        }

        // Draw and resize image
        ctx.drawImage(img, 0, 0, targetWidth, targetHeight);

        // Convert to PNG and get base64 (without data URL prefix)
        const dataUrl = canvas.toDataURL('image/png');
        const base64 = dataUrl.split(',')[1];

        // Clean up
        URL.revokeObjectURL(img.src);

        resolve(base64);
      } catch (error) {
        reject(error);
      }
    };

    img.onerror = () => {
      reject(new Error('Failed to load image'));
    };

    // Load the image
    img.src = URL.createObjectURL(file);
  });
};

const createEmptyTeam = (): TeamInfo => ({
  color: { r: 255, g: 0, b: 0, a: 255 },
  names: {},
  shortNames: {},
  albedoImage: "",
});

const createEmptyDialogue = (): Record<string, DialogueLine[]> => {
  const dialogue: Record<string, DialogueLine[]> = {};
  LANGUAGES.forEach(lang => {
    dialogue[lang.id] = [];
  });
  return dialogue;
};

// Helper to clamp number between min and max
const clamp = (value: number, min: number, max: number): number => {
  return Math.max(min, Math.min(max, value));
};

// Helper to convert ISO datetime to datetime-local format (YYYY-MM-DDTHH:mm)
const toDatetimeLocal = (isoString: string): string => {
  if (!isoString) return '';
  // Remove seconds and timezone info if present
  return isoString.slice(0, 16); // YYYY-MM-DDTHH:mm
};

// Helper to convert datetime-local to ISO format
const toISOString = (datetimeLocal: string): string => {
  if (!datetimeLocal) return '';
  return datetimeLocal; // Keep as is for JSON storage
};

export default function SplatfestEditor() {
  const [data, setData] = useState<SplatfestData>({
    id: 1000,
    battleResultRate: 1,
    lowPopulationNotJP: true,
    separateMatchingJP: true,
    gamemode: "turf_war",
    backupLanguage: "eu_en",
    stages: [],
    teamA: createEmptyTeam(),
    teamB: createEmptyTeam(),
    neutralColor: { r: 128, g: 128, b: 128, a: 255 },
    panelImage: "",
    announceTime: "",
    startTime: "",
    endTime: "",
    resultTime: "",
    afterFesBonusStart: "",
    announceNews: createEmptyDialogue(),
    startNews: createEmptyDialogue(),
    resultANews: createEmptyDialogue(),
    resultBNews: createEmptyDialogue(),
  });

  const [errors, setErrors] = useState<string[]>([]);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const teamAImageRef = useRef<HTMLInputElement>(null);
  const teamBImageRef = useRef<HTMLInputElement>(null);
  const panelImageRef = useRef<HTMLInputElement>(null);

  // Validation
  const validateData = (): string[] => {
    const errors: string[] = [];

    if (!data.id || data.id < 1000 || data.id > 9999) errors.push("Festival ID must be between 1000 and 9999");
    if (data.stages.length !== 3) errors.push("Exactly 3 stages must be selected");
    if (!data.teamA.albedoImage) errors.push("Team Alpha albedo image is required");
    if (!data.teamB.albedoImage) errors.push("Team Bravo albedo image is required");
    if (!data.panelImage) errors.push("Panel image is required");
    if (!data.announceTime) errors.push("Announcement time is required");
    if (!data.startTime) errors.push("Start time is required");
    if (!data.endTime) errors.push("End time is required");
    if (!data.resultTime) errors.push("Result time is required");
    if (!data.afterFesBonusStart) errors.push("After-Fes bonus start time is required");

    // Validate times
    if (data.announceTime && data.startTime && new Date(data.announceTime) >= new Date(data.startTime)) {
      errors.push("Announcement time must be before start time");
    }
    if (data.startTime && data.endTime && new Date(data.startTime) >= new Date(data.endTime)) {
      errors.push("Start time must be before end time");
    }
    if (data.endTime && data.resultTime && new Date(data.endTime) >= new Date(data.resultTime)) {
      errors.push("End time must be before result time");
    }

    // Validate team names
    LANGUAGES.forEach(lang => {
      if (!data.teamA.names[lang.id]) errors.push(`Team Alpha name for ${lang.label} is required`);
      if (!data.teamA.shortNames[lang.id]) errors.push(`Team Alpha short name for ${lang.label} is required`);
      if (!data.teamB.names[lang.id]) errors.push(`Team Bravo name for ${lang.label} is required`);
      if (!data.teamB.shortNames[lang.id]) errors.push(`Team Bravo short name for ${lang.label} is required`);
    });

    // Validate backup language has dialogue for all news types
    const backupLangLabel = LANGUAGES.find(l => l.id === data.backupLanguage)?.label || data.backupLanguage;

    if (!data.announceNews[data.backupLanguage] || data.announceNews[data.backupLanguage].length === 0) {
      errors.push(`Backup language (${backupLangLabel}) must have at least one dialogue line for Announcement News`);
    }
    if (!data.startNews[data.backupLanguage] || data.startNews[data.backupLanguage].length === 0) {
      errors.push(`Backup language (${backupLangLabel}) must have at least one dialogue line for Start News`);
    }
    if (!data.resultANews[data.backupLanguage] || data.resultANews[data.backupLanguage].length === 0) {
      errors.push(`Backup language (${backupLangLabel}) must have at least one dialogue line for Team A Result News`);
    }
    if (!data.resultBNews[data.backupLanguage] || data.resultBNews[data.backupLanguage].length === 0) {
      errors.push(`Backup language (${backupLangLabel}) must have at least one dialogue line for Team B Result News`);
    }

    return errors;
  };

  // Image handling
  const handleImageUpload = async (file: File | null, team: 'A' | 'B' | 'panel') => {
    if (!file) return;

    const targetSize = team === 'panel' ? { width: 960, height: 540 } : { width: 512, height: 512 };

    try {
      // Process and resize image automatically
      const base64 = await processAndResizeImage(file, targetSize.width, targetSize.height);

      if (team === 'A') {
        setData(prev => ({ ...prev, teamA: { ...prev.teamA, albedoImage: base64 } }));
      } else if (team === 'B') {
        setData(prev => ({ ...prev, teamB: { ...prev.teamB, albedoImage: base64 } }));
      } else {
        setData(prev => ({ ...prev, panelImage: base64 }));
      }

      // Show success message with info
      const sizeKB = (base64.length * 0.75 / 1024).toFixed(1);
      console.log(`Image processed and resized to ${targetSize.width}x${targetSize.height} (${sizeKB} KB)`);
    } catch (error) {
      alert('Failed to process image. Please try a different file.');
      console.error('Image processing error:', error);
    }
  };

  // Dialogue management
  const addDialogueLine = (newsType: 'announceNews' | 'startNews' | 'resultANews' | 'resultBNews', language: string) => {
    const newLine: DialogueLine = {
      command: "speak_raw_text",
      emotion: "normal",
      speaker: "callie",
      text: "",
      waitButton: false,
    };

    setData(prev => ({
      ...prev,
      [newsType]: {
        ...prev[newsType],
        [language]: [...prev[newsType][language], newLine]
      }
    }));
  };

  const removeDialogueLine = (newsType: 'announceNews' | 'startNews' | 'resultANews' | 'resultBNews', language: string, index: number) => {
    setData(prev => ({
      ...prev,
      [newsType]: {
        ...prev[newsType],
        [language]: prev[newsType][language].filter((_, i) => i !== index)
      }
    }));
  };

  const updateDialogueLine = (
    newsType: 'announceNews' | 'startNews' | 'resultANews' | 'resultBNews',
    language: string,
    index: number,
    field: keyof DialogueLine,
    value: string | boolean
  ) => {
    setData(prev => ({
      ...prev,
      [newsType]: {
        ...prev[newsType],
        [language]: prev[newsType][language].map((line, i) =>
          i === index ? { ...line, [field]: value } : line
        )
      }
    }));
  };

  // JSON export/import
  const downloadJSON = () => {
    const validationErrors = validateData();
    if (validationErrors.length > 0) {
      setErrors(validationErrors);
      alert('Please fix all errors before downloading JSON');
      return;
    }

    const json = JSON.stringify(data, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `splatfest-${data.id}.json`;
    a.click();
    URL.revokeObjectURL(url);
    setErrors([]);
  };

  const loadJSON = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      try {
        const json = JSON.parse(e.target?.result as string);
        setData(json);
        setErrors([]);
        alert('JSON loaded successfully!');
      } catch (error) {
        alert('Failed to parse JSON file');
      }
    };
    reader.readAsText(file);
  };

  const saveSplatfest = () => {
    const validationErrors = validateData();
    if (validationErrors.length > 0) {
      setErrors(validationErrors);
      alert('Please fix all errors before saving');
      return;
    }

    // TODO: Send data to server via gRPC
    console.log('Saving splatfest:', data);
    alert('Save to server not yet implemented');
    setErrors([]);
  };
  return (
      <div className="space-y-6">
          {/* Hidden file input for JSON loading */}
          <input
              ref={fileInputRef}
              type="file"
              accept=".json"
              onChange={loadJSON}
              className="hidden"
          />

          {/* Errors display */}
          {errors.length > 0 && (
              <Card className="border-red-500/50 bg-red-500/5">
                  <CardHeader>
                      <CardTitle className="text-red-500">Validation Errors</CardTitle>
                  </CardHeader>
                  <CardContent>
                      <ul className="list-disc list-inside space-y-1 text-sm text-red-500">
                          {errors.map((error, idx) => (
                              <li key={idx}>{error}</li>
                          ))}
                      </ul>
                  </CardContent>
              </Card>
          )}

          <div className="flex items-center justify-between">
              <div>
                  <Link to="/settings" className="text-sm text-muted-foreground hover:text-foreground">
                      ← Back to Settings
                  </Link>
                  <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight mt-2">
                      Create Splatfest
                  </h1>
                  <p className="text-muted-foreground mt-2">
                      Configure all aspects of your Splatfest event
                  </p>
              </div>
              <div className="flex gap-2">
                  <Button variant="outline" onClick={() => fileInputRef.current?.click()}>
                      <Upload className="mr-2 h-4 w-4" />
                      Load JSON
                  </Button>
                  <Button variant="outline" onClick={downloadJSON}>
                      <Download className="mr-2 h-4 w-4" />
                      Download JSON
                  </Button>
                  <Button onClick={saveSplatfest}>
                      <Sparkles className="mr-2 h-4 w-4" />
                      Save Splatfest
                  </Button>
              </div>
          </div>

          <Tabs defaultValue="basic" className="space-y-4">
              <TabsList className="grid w-full grid-cols-5">
                  <TabsTrigger value="basic">Basic Info</TabsTrigger>
                  <TabsTrigger value="teams">Teams</TabsTrigger>
                  <TabsTrigger value="dialogue">Dialogue</TabsTrigger>
                  <TabsTrigger value="stages">Stages & Mode</TabsTrigger>
                  <TabsTrigger value="timing">Timing</TabsTrigger>
              </TabsList>

              {/* Basic Info Tab */}
              <TabsContent value="basic" className="space-y-4">
                  <Card>
                      <CardHeader>
                          <CardTitle>Basic Information</CardTitle>
                          <CardDescription>Set the fundamental properties of your Splatfest</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="grid grid-cols-2 gap-4">
                              <div className="space-y-2">
                                  <Label htmlFor="festId">Festival ID *</Label>
                                  <Input
                                      id="festId"
                                      type="number"
                                      placeholder="1000"
                                      min="1000"
                                      max="9999"
                                      value={data.id}
                                      onChange={(e) => {
                                          const val = parseInt(e.target.value) || 1000;
                                          setData(prev => ({ ...prev, id: clamp(val, 1000, 9999) }));
                                      }}
                                      onBlur={(e) => {
                                          const val = parseInt(e.target.value) || 1000;
                                          setData(prev => ({ ...prev, id: clamp(val, 1000, 9999) }));
                                      }}
                                  />
                                  <p className="text-xs text-muted-foreground">Unique identifier (1000-9999)</p>
                              </div>

                              <div className="space-y-2">
                                  <Label htmlFor="battleResultRate">Battle Result Rate</Label>
                                  <Input
                                      id="battleResultRate"
                                      type="number"
                                      placeholder="1"
                                      min="1"
                                      value={data.battleResultRate}
                                      onChange={(e) => setData(prev => ({ ...prev, battleResultRate: parseInt(e.target.value) || 1 }))}
                                  />
                                  <p className="text-xs text-muted-foreground">Weight for battle results (default: 1)</p>
                              </div>
                          </div>

                          <div className="grid grid-cols-2 gap-4">
                              <div className="space-y-2">
                                  <Label htmlFor="backupLanguage">Backup Language *</Label>
                                  <Select
                                      value={data.backupLanguage}
                                      onValueChange={(value) => setData(prev => ({ ...prev, backupLanguage: value }))}
                                  >
                                      <SelectTrigger id="backupLanguage">
                                          <SelectValue />
                                      </SelectTrigger>
                                      <SelectContent>
                                          {LANGUAGES.map(lang => (
                                              <SelectItem key={lang.id} value={lang.id}>
                                                  {lang.flag} {lang.label}
                                              </SelectItem>
                                          ))}
                                      </SelectContent>
                                  </Select>
                                  <p className="text-xs text-muted-foreground">Used when a language is not available</p>
                              </div>
                          </div>

                          <Separator />

                          <div className="space-y-4">
                              <Label>Regional Settings</Label>
                              <div className="space-y-3">
                                  <div className="flex items-center justify-between">
                                      <div className="space-y-0.5">
                                          <Label className="font-normal">Low Population Outside Japan</Label>
                                          <p className="text-xs text-muted-foreground">
                                              Enable for low population regions
                                          </p>
                                      </div>
                                      <input
                                          type="checkbox"
                                          checked={data.lowPopulationNotJP}
                                          onChange={(e) => setData(prev => ({ ...prev, lowPopulationNotJP: e.target.checked }))}
                                          className="h-4 w-4"
                                      />
                                  </div>
                                  <div className="flex items-center justify-between">
                                      <div className="space-y-0.5">
                                          <Label className="font-normal">Separate Matching (Japan)</Label>
                                          <p className="text-xs text-muted-foreground">
                                              Separate matchmaking for Japanese region
                                          </p>
                                      </div>
                                      <input
                                          type="checkbox"
                                          checked={data.separateMatchingJP}
                                          onChange={(e) => setData(prev => ({ ...prev, separateMatchingJP: e.target.checked }))}
                                          className="h-4 w-4"
                                      />
                                  </div>
                              </div>
                          </div>
                      </CardContent>
                  </Card>
              </TabsContent>

              {/* Teams Tab */}
              <TabsContent value="teams" className="space-y-4">
                  {/* Hidden file inputs */}
                  <input
                      ref={teamAImageRef}
                      type="file"
                      accept="image/*"
                      onChange={(e) => handleImageUpload(e.target.files?.[0] || null, 'A')}
                      className="hidden"
                  />
                  <input
                      ref={teamBImageRef}
                      type="file"
                      accept="image/*"
                      onChange={(e) => handleImageUpload(e.target.files?.[0] || null, 'B')}
                      className="hidden"
                  />

                  {["Team Alpha", "Team Bravo"].map((teamName, teamIdx) => {
                      const team = teamIdx === 0 ? data.teamA : data.teamB;
                      const setTeam = (updates: Partial<TeamInfo>) => {
                          const key = teamIdx === 0 ? 'teamA' : 'teamB';
                          setData(prev => ({ ...prev, [key]: { ...prev[key], ...updates } }));
                      };

                      return (
                          <Card key={teamName}>
                              <CardHeader>
                                  <CardTitle>{teamName}</CardTitle>
                                  <CardDescription>Configure team colors, names, and albedo texture</CardDescription>
                              </CardHeader>
                              <CardContent className="space-y-4">
                                  {/* Team Albedo Image */}
                                  <div className="space-y-2">
                                      <Label>Team T-Shirt Albedo Texture (512x512 PNG) *</Label>
                                      <div className="flex items-start gap-4">
                                          <div className="space-y-2">
                                              <Button
                                                  type="button"
                                                  variant="outline"
                                                  onClick={() => teamIdx === 0 ? teamAImageRef.current?.click() : teamBImageRef.current?.click()}
                                              >
                                                  <ImageIcon className="mr-2 h-4 w-4" />
                                                  {team.albedoImage ? 'Change Image' : 'Upload Image'}
                                              </Button>
                                              {team.albedoImage && (
                                                  <div className="flex items-center gap-2 text-sm text-green-600">
                                                      <span>✓ Image loaded ({(team.albedoImage.length * 0.75 / 1024).toFixed(1)} KB)</span>
                                                  </div>
                                              )}
                                          </div>
                                          {team.albedoImage && (
                                              <div className="border rounded-lg p-2 bg-muted/30">
                                                  <img
                                                      src={`data:image/png;base64,${team.albedoImage}`}
                                                      alt={`${teamName} preview`}
                                                      className="w-32 h-32 object-contain"
                                                  />
                                                  <p className="text-xs text-center text-muted-foreground mt-1">Preview</p>
                                              </div>
                                          )}
                                      </div>
                                      <p className="text-xs text-muted-foreground">
                                          Upload any image - it will be automatically resized to 512x512 and converted to PNG
                                      </p>
                                  </div>

                                  <Separator />

                                  <div className="space-y-2">
                                      <Label htmlFor={`team${teamIdx}Color`}>Team Color (RGBA) *</Label>
                                      <div className="grid grid-cols-5 gap-2">
                                          <div>
                                              <Label className="text-xs">Red</Label>
                                              <Input
                                                  type="number"
                                                  min="0"
                                                  max="255"
                                                  value={team.color.r}
                                                  onChange={(e) => {
                                                      const val = parseInt(e.target.value) || 0;
                                                      setTeam({ color: { ...team.color, r: clamp(val, 0, 255) } });
                                                  }}
                                                  onBlur={(e) => {
                                                      const val = parseInt(e.target.value) || 0;
                                                      setTeam({ color: { ...team.color, r: clamp(val, 0, 255) } });
                                                  }}
                                              />
                                          </div>
                                          <div>
                                              <Label className="text-xs">Green</Label>
                                              <Input
                                                  type="number"
                                                  min="0"
                                                  max="255"
                                                  value={team.color.g}
                                                  onChange={(e) => {
                                                      const val = parseInt(e.target.value) || 0;
                                                      setTeam({ color: { ...team.color, g: clamp(val, 0, 255) } });
                                                  }}
                                                  onBlur={(e) => {
                                                      const val = parseInt(e.target.value) || 0;
                                                      setTeam({ color: { ...team.color, g: clamp(val, 0, 255) } });
                                                  }}
                                              />
                                          </div>
                                          <div>
                                              <Label className="text-xs">Blue</Label>
                                              <Input
                                                  type="number"
                                                  min="0"
                                                  max="255"
                                                  value={team.color.b}
                                                  onChange={(e) => {
                                                      const val = parseInt(e.target.value) || 0;
                                                      setTeam({ color: { ...team.color, b: clamp(val, 0, 255) } });
                                                  }}
                                                  onBlur={(e) => {
                                                      const val = parseInt(e.target.value) || 0;
                                                      setTeam({ color: { ...team.color, b: clamp(val, 0, 255) } });
                                                  }}
                                              />
                                          </div>
                                          <div>
                                              <Label className="text-xs">Alpha</Label>
                                              <Input
                                                  type="number"
                                                  min="0"
                                                  max="255"
                                                  value={team.color.a}
                                                  onChange={(e) => {
                                                      const val = parseInt(e.target.value) || 255;
                                                      setTeam({ color: { ...team.color, a: clamp(val, 0, 255) } });
                                                  }}
                                                  onBlur={(e) => {
                                                      const val = parseInt(e.target.value) || 255;
                                                      setTeam({ color: { ...team.color, a: clamp(val, 0, 255) } });
                                                  }}
                                              />
                                          </div>
                                          <div className="flex flex-col items-center justify-end gap-1">
                                              <Label className="text-xs">Color Picker</Label>
                                              <input
                                                  type="color"
                                                  title="Pick a color"
                                                  className="w-full h-10 rounded cursor-pointer border-2"
                                                  style={{backgroundColor: `rgba(${team.color.r}, ${team.color.g}, ${team.color.b}, ${team.color.a / 255})`}}
                                                  value={`#${team.color.r.toString(16).padStart(2, '0')}${team.color.g.toString(16).padStart(2, '0')}${team.color.b.toString(16).padStart(2, '0')}`}
                                                  onChange={(e) => {
                                                      const hex = e.target.value;
                                                      const r = parseInt(hex.slice(1, 3), 16);
                                                      const g = parseInt(hex.slice(3, 5), 16);
                                                      const b = parseInt(hex.slice(5, 7), 16);
                                                      setTeam({ color: { ...team.color, r, g, b } });
                                                  }}
                                              />
                                          </div>
                                      </div>
                                  </div>

                                  <Separator />

                                  <div className="space-y-3">
                                      <Label>Team Names by Language *</Label>
                                      {LANGUAGES.map(lang => (
                                          <div key={lang.id} className="grid grid-cols-2 gap-4">
                                              <div className="space-y-1">
                                                  <Label className="text-xs flex items-center gap-2">
                                                      <span>{lang.flag}</span>
                                                      <span>{lang.label} - Full Name</span>
                                                  </Label>
                                                  <Input
                                                      placeholder={`${teamName} full name`}
                                                      value={team.names[lang.id] || ''}
                                                      onChange={(e) => setTeam({ names: { ...team.names, [lang.id]: e.target.value } })}
                                                  />
                                              </div>
                                              <div className="space-y-1">
                                                  <Label className="text-xs">Short Name</Label>
                                                  <Input
                                                      placeholder={`${teamName} short`}
                                                      maxLength={10}
                                                      value={team.shortNames[lang.id] || ''}
                                                      onChange={(e) => setTeam({ shortNames: { ...team.shortNames, [lang.id]: e.target.value } })}
                                                  />
                                              </div>
                                          </div>
                                      ))}
                                  </div>
                              </CardContent>
                          </Card>
                      );
                  })}

                  <Card>
                      <CardHeader>
                          <CardTitle>Neutral Color</CardTitle>
                          <CardDescription>Color used for neutral/non-team elements</CardDescription>
                      </CardHeader>
                      <CardContent>
                          <div className="grid grid-cols-5 gap-2">
                              <div>
                                  <Label className="text-xs">Red</Label>
                                  <Input
                                      type="number"
                                      min="0"
                                      max="255"
                                      value={data.neutralColor.r}
                                      onChange={(e) => {
                                          const val = parseInt(e.target.value) || 0;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, r: clamp(val, 0, 255) } }));
                                      }}
                                      onBlur={(e) => {
                                          const val = parseInt(e.target.value) || 0;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, r: clamp(val, 0, 255) } }));
                                      }}
                                  />
                              </div>
                              <div>
                                  <Label className="text-xs">Green</Label>
                                  <Input
                                      type="number"
                                      min="0"
                                      max="255"
                                      value={data.neutralColor.g}
                                      onChange={(e) => {
                                          const val = parseInt(e.target.value) || 0;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, g: clamp(val, 0, 255) } }));
                                      }}
                                      onBlur={(e) => {
                                          const val = parseInt(e.target.value) || 0;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, g: clamp(val, 0, 255) } }));
                                      }}
                                  />
                              </div>
                              <div>
                                  <Label className="text-xs">Blue</Label>
                                  <Input
                                      type="number"
                                      min="0"
                                      max="255"
                                      value={data.neutralColor.b}
                                      onChange={(e) => {
                                          const val = parseInt(e.target.value) || 0;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, b: clamp(val, 0, 255) } }));
                                      }}
                                      onBlur={(e) => {
                                          const val = parseInt(e.target.value) || 0;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, b: clamp(val, 0, 255) } }));
                                      }}
                                  />
                              </div>
                              <div>
                                  <Label className="text-xs">Alpha</Label>
                                  <Input
                                      type="number"
                                      min="0"
                                      max="255"
                                      value={data.neutralColor.a}
                                      onChange={(e) => {
                                          const val = parseInt(e.target.value) || 255;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, a: clamp(val, 0, 255) } }));
                                      }}
                                      onBlur={(e) => {
                                          const val = parseInt(e.target.value) || 255;
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, a: clamp(val, 0, 255) } }));
                                      }}
                                  />
                              </div>
                              <div className="flex flex-col items-center justify-end gap-1">
                                  <Label className="text-xs">Color Picker</Label>
                                  <input
                                      type="color"
                                      title="Pick a color"
                                      className="w-full h-10 rounded cursor-pointer border-2"
                                      style={{backgroundColor: `rgba(${data.neutralColor.r}, ${data.neutralColor.g}, ${data.neutralColor.b}, ${data.neutralColor.a / 255})`}}
                                      value={`#${data.neutralColor.r.toString(16).padStart(2, '0')}${data.neutralColor.g.toString(16).padStart(2, '0')}${data.neutralColor.b.toString(16).padStart(2, '0')}`}
                                      onChange={(e) => {
                                          const hex = e.target.value;
                                          const r = parseInt(hex.slice(1, 3), 16);
                                          const g = parseInt(hex.slice(3, 5), 16);
                                          const b = parseInt(hex.slice(5, 7), 16);
                                          setData(prev => ({ ...prev, neutralColor: { ...prev.neutralColor, r, g, b } }));
                                      }}
                                  />
                              </div>
                          </div>
                      </CardContent>
                  </Card>
              </TabsContent>

              {/* Dialogue Tab */}
              <TabsContent value="dialogue" className="space-y-4">
                  <div className="rounded-lg border border-blue-500/50 bg-blue-500/5 p-4">
                      <div className="flex items-start gap-3">
                          <AlertCircle className="h-5 w-5 text-blue-500 mt-0.5" />
                          <div className="space-y-1">
                              <p className="text-sm font-medium text-blue-500">Dialogue Configuration</p>
                              <p className="text-sm text-muted-foreground">
                                  Configure dialogue for 4 news sequences: Announcement, Start, Team A Result, Team B Result.
                                  Each sequence can have multiple dialogue lines with different speakers and emotions.
                              </p>
                          </div>
                      </div>
                  </div>

                  {[
                      {name: "Announcement News", key: "announceNews" as const, desc: "Shown when festival is announced"},
                      {name: "Start News", key: "startNews" as const, desc: "Shown when festival starts"},
                      {name: "Team A Result News", key: "resultANews" as const, desc: "Shown when Team Alpha wins"},
                      {name: "Team B Result News", key: "resultBNews" as const, desc: "Shown when Team Bravo wins"}
                  ].map(newsType => (
                      <Card key={newsType.key}>
                          <CardHeader>
                              <CardTitle>{newsType.name}</CardTitle>
                              <CardDescription>{newsType.desc}</CardDescription>
                          </CardHeader>
                          <CardContent className="space-y-4">
                              {/* Language tabs for dialogue */}
                              <Tabs defaultValue={LANGUAGES[0].id}>
                                  <TabsList className="grid w-full grid-cols-9">
                                      {LANGUAGES.map(lang => (
                                          <TabsTrigger key={lang.id} value={lang.id} className="text-xs">
                                              {lang.flag}
                                          </TabsTrigger>
                                      ))}
                                  </TabsList>

                                  {LANGUAGES.map(lang => {
                                      const lines = data[newsType.key][lang.id] || [];

                                      return (
                                          <TabsContent key={lang.id} value={lang.id} className="space-y-3">
                                              <div className="flex items-center justify-between mb-2">
                                                  <Label className="text-sm font-medium">{lang.label}</Label>
                                                  <Button
                                                      size="sm"
                                                      variant="outline"
                                                      onClick={() => addDialogueLine(newsType.key, lang.id)}
                                                  >
                                                      <Plus className="mr-2 h-3 w-3" />
                                                      Add Line for {lang.label}
                                                  </Button>
                                              </div>

                                              {lines.length === 0 ? (
                                                  <div className="text-center py-8 text-sm text-muted-foreground border-2 border-dashed rounded">
                                                      No dialogue lines yet. Click "Add Line" above.
                                                  </div>
                                              ) : (
                                                  lines.map((line, lineIdx) => (
                                                      <Card key={lineIdx} className="bg-muted/30">
                                                          <CardContent className="pt-4 space-y-3">
                                                              <div className="grid grid-cols-3 gap-2">
                                                                  <div className="space-y-1">
                                                                      <Label className="text-xs">Speaker</Label>
                                                                      <Select
                                                                          value={line.speaker}
                                                                          onValueChange={(value) => updateDialogueLine(newsType.key, lang.id, lineIdx, 'speaker', value)}
                                                                      >
                                                                          <SelectTrigger className="h-8">
                                                                              <SelectValue />
                                                                          </SelectTrigger>
                                                                          <SelectContent>
                                                                              {SPEAKERS.map(speaker => (
                                                                                  <SelectItem key={speaker.id} value={speaker.id}>
                                                                                      {speaker.label}
                                                                                  </SelectItem>
                                                                              ))}
                                                                          </SelectContent>
                                                                      </Select>
                                                                  </div>
                                                                  <div className="space-y-1">
                                                                      <Label className="text-xs">Emotion</Label>
                                                                      <Select
                                                                          value={line.emotion}
                                                                          onValueChange={(value) => updateDialogueLine(newsType.key, lang.id, lineIdx, 'emotion', value)}
                                                                      >
                                                                          <SelectTrigger className="h-8">
                                                                              <SelectValue />
                                                                          </SelectTrigger>
                                                                          <SelectContent>
                                                                              {EMOTIONS.map(emotion => (
                                                                                  <SelectItem key={emotion.id} value={emotion.id}>
                                                                                      {emotion.label}
                                                                                  </SelectItem>
                                                                              ))}
                                                                          </SelectContent>
                                                                      </Select>
                                                                  </div>
                                                                  <div className="space-y-1">
                                                                      <Label className="text-xs">Wait for Button</Label>
                                                                      <div className="flex items-center h-8">
                                                                          <input
                                                                              type="checkbox"
                                                                              className="h-4 w-4"
                                                                              checked={line.waitButton}
                                                                              onChange={(e) => updateDialogueLine(newsType.key, lang.id, lineIdx, 'waitButton', e.target.checked)}
                                                                          />
                                                                      </div>
                                                                  </div>
                                                              </div>
                                                              <div className="space-y-1">
                                                                  <Label className="text-xs">Dialogue Text</Label>
                                                                  <Textarea
                                                                      placeholder="Enter dialogue text..."
                                                                      className="min-h-[60px] resize-none"
                                                                      value={line.text}
                                                                      onChange={(e) => updateDialogueLine(newsType.key, lang.id, lineIdx, 'text', e.target.value)}
                                                                  />
                                                              </div>
                                                              <div className="flex justify-end">
                                                                  <Button
                                                                      size="sm"
                                                                      variant="ghost"
                                                                      onClick={() => removeDialogueLine(newsType.key, lang.id, lineIdx)}
                                                                  >
                                                                      <Trash2 className="h-3 w-3 mr-1" />
                                                                      Remove
                                                                  </Button>
                                                              </div>
                                                          </CardContent>
                                                      </Card>
                                                  ))
                                              )}
                                          </TabsContent>
                                      );
                                  })}
                              </Tabs>
                          </CardContent>
                      </Card>
                  ))}
              </TabsContent>

              {/* Stages & Mode Tab */}
              <TabsContent value="stages" className="space-y-4">
                  {/* Hidden file input for panel image */}
                  <input
                      ref={panelImageRef}
                      type="file"
                      accept="image/*"
                      onChange={(e) => handleImageUpload(e.target.files?.[0] || null, 'panel')}
                      className="hidden"
                  />

                  {/* Panel Image */}
                  <Card>
                      <CardHeader>
                          <CardTitle>Festival Panel Image</CardTitle>
                          <CardDescription>Panel image for news and matchmaking menu (960x540 PNG) *</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="flex items-start gap-4">
                              <div className="space-y-2">
                                  <Button
                                      type="button"
                                      variant="outline"
                                      onClick={() => panelImageRef.current?.click()}
                                  >
                                      <ImageIcon className="mr-2 h-4 w-4" />
                                      {data.panelImage ? 'Change Image' : 'Upload Image'}
                                  </Button>
                                  {data.panelImage && (
                                      <div className="flex items-center gap-2 text-sm text-green-600">
                                          <span>✓ Image loaded ({(data.panelImage.length * 0.75 / 1024).toFixed(1)} KB)</span>
                                      </div>
                                  )}
                              </div>
                              {data.panelImage && (
                                  <div className="border rounded-lg p-2 bg-muted/30">
                                      <img
                                          src={`data:image/png;base64,${data.panelImage}`}
                                          alt="Panel preview"
                                          className="w-64 h-36 object-contain"
                                      />
                                      <p className="text-xs text-center text-muted-foreground mt-1">Preview (960x540)</p>
                                  </div>
                              )}
                          </div>
                          <p className="text-xs text-muted-foreground">
                              Upload any image - it will be automatically resized to 960x540 and converted to PNG
                          </p>
                      </CardContent>
                  </Card>

                  <Card>
                      <CardHeader>
                          <CardTitle>Game Mode</CardTitle>
                          <CardDescription>Select the game mode for this Splatfest</CardDescription>
                      </CardHeader>
                      <CardContent>
                          <Select
                              value={data.gamemode}
                              onValueChange={(value) => setData(prev => ({ ...prev, gamemode: value }))}
                          >
                              <SelectTrigger>
                                  <SelectValue />
                              </SelectTrigger>
                              <SelectContent>
                                  {GAMEMODES.map(mode => (
                                      <SelectItem key={mode.id} value={mode.id}>
                                          {mode.label}
                                      </SelectItem>
                                  ))}
                              </SelectContent>
                          </Select>
                      </CardContent>
                  </Card>

                  <Card>
                      <CardHeader>
                          <CardTitle>Festival Stages</CardTitle>
                          <CardDescription>Select exactly 3 stages for this Splatfest</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          {[0, 1, 2].map((stageIdx) => (
                              <div key={stageIdx} className="space-y-2">
                                  <Label>Stage {stageIdx + 1} *</Label>
                                  <Select
                                      value={data.stages[stageIdx]?.toString() || ""}
                                      onValueChange={(value) => {
                                          const newStages = [...data.stages];
                                          newStages[stageIdx] = parseInt(value);
                                          setData(prev => ({ ...prev, stages: newStages }));
                                      }}
                                  >
                                      <SelectTrigger>
                                          <SelectValue placeholder="Select a stage" />
                                      </SelectTrigger>
                                      <SelectContent>
                                          {STAGES.map(stage => (
                                              <SelectItem key={stage.id} value={stage.id.toString()}>
                                                  {stage.name}
                                              </SelectItem>
                                          ))}
                                      </SelectContent>
                                  </Select>
                              </div>
                          ))}

                          <div className="rounded-lg border border-yellow-500/50 bg-yellow-500/5 p-4 mt-4">
                              <p className="text-sm text-muted-foreground">
                                  💡 <strong>Tip:</strong> Choose diverse stages to keep the festival interesting!
                                  Popular choices include Urchin Underpass, Walleye Warehouse, and Blackbelly Skatepark.
                              </p>
                          </div>
                      </CardContent>
                  </Card>
              </TabsContent>

              {/* Timing Tab */}
              <TabsContent value="timing" className="space-y-4">
                  <Card>
                      <CardHeader>
                          <CardTitle>Festival Timeline</CardTitle>
                          <CardDescription>Set the dates and times for each phase of the Splatfest</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="grid grid-cols-2 gap-4">
                              <div className="space-y-2">
                                  <Label htmlFor="announceTime">Announcement Time *</Label>
                                  <Input
                                      id="announceTime"
                                      type="datetime-local"
                                      value={toDatetimeLocal(data.announceTime)}
                                      onChange={(e) => setData(prev => ({ ...prev, announceTime: toISOString(e.target.value) }))}
                                  />
                                  <p className="text-xs text-muted-foreground">When the festival is announced</p>
                              </div>

                              <div className="space-y-2">
                                  <Label htmlFor="startTime">Start Time *</Label>
                                  <Input
                                      id="startTime"
                                      type="datetime-local"
                                      value={toDatetimeLocal(data.startTime)}
                                      onChange={(e) => setData(prev => ({ ...prev, startTime: toISOString(e.target.value) }))}
                                  />
                                  <p className="text-xs text-muted-foreground">When voting and battles begin</p>
                              </div>

                              <div className="space-y-2">
                                  <Label htmlFor="endTime">End Time *</Label>
                                  <Input
                                      id="endTime"
                                      type="datetime-local"
                                      value={toDatetimeLocal(data.endTime)}
                                      onChange={(e) => setData(prev => ({ ...prev, endTime: toISOString(e.target.value) }))}
                                  />
                                  <p className="text-xs text-muted-foreground">When the festival ends</p>
                              </div>

                              <div className="space-y-2">
                                  <Label htmlFor="resultTime">Result Time *</Label>
                                  <Input
                                      id="resultTime"
                                      type="datetime-local"
                                      value={toDatetimeLocal(data.resultTime)}
                                      onChange={(e) => setData(prev => ({ ...prev, resultTime: toISOString(e.target.value) }))}
                                  />
                                  <p className="text-xs text-muted-foreground">When results are announced</p>
                              </div>

                              <div className="space-y-2 col-span-2">
                                  <Label htmlFor="bonusTime">After-Fes Bonus Start *</Label>
                                  <Input
                                      id="bonusTime"
                                      type="datetime-local"
                                      value={toDatetimeLocal(data.afterFesBonusStart)}
                                      onChange={(e) => setData(prev => ({ ...prev, afterFesBonusStart: toISOString(e.target.value) }))}
                                  />
                                  <p className="text-xs text-muted-foreground">When post-festival bonuses start</p>
                              </div>
                          </div>

                          <Separator />

                          <div className="rounded-lg border border-blue-500/50 bg-blue-500/5 p-4">
                              <div className="flex items-start gap-3">
                                  <AlertCircle className="h-5 w-5 text-blue-500 mt-0.5" />
                                  <div className="space-y-2">
                                      <p className="text-sm font-medium text-blue-500">Timing Guidelines</p>
                                      <ul className="text-sm text-muted-foreground space-y-1">
                                          <li>• Announce Time should be before Start Time</li>
                                          <li>• Start Time should be before End Time</li>
                                          <li>• Result Time should be after End Time</li>
                                          <li>• Festival can be in the past but must always have one active</li>
                                      </ul>
                                  </div>
                              </div>
                          </div>
                      </CardContent>
                  </Card>
              </TabsContent>
          </Tabs>
      </div>
  )
}

