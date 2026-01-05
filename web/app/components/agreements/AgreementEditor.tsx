import { useState, useEffect } from "react";
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "~/components/ui/dialog";
import { Button } from "~/components/ui/button";
import { Label } from "~/components/ui/label";
import { Input } from "~/components/ui/input";
import { Textarea } from "~/components/ui/textarea";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "~/components/ui/select";
import type { Agreement } from "~/types/agreement";
import { AGREEMENT_TYPES } from "~/constants/agreement-types";
import countriesLanguages from "~/data/countries_languages.json";

interface AgreementEditorProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  agreement?: Agreement | null;
  onSave: (agreement: Agreement) => Promise<void>;
}


export function AgreementEditor({ open, onOpenChange, agreement, onSave }: AgreementEditorProps) {
  const [formData, setFormData] = useState<Omit<Agreement, 'publishDate'>>({
    type: "NINTENDO-NETWORK-EULA",
    version: 1,
    country: "",
    language: "",
    languageName: "",
    mainTitle: "",
    subTitle: "",
    mainContent: "",
    subContent: "",
    agreeButtonText: "",
    disagreeButtonText: "",
  });
  const [isSaving, setIsSaving] = useState(false);
  const [availableLanguages, setAvailableLanguages] = useState<Array<{ code: string; name: string; native?: string }>>([]);

  useEffect(() => {
    if (agreement) {
      setFormData({
        type: agreement.type,
        version: agreement.version,
        country: agreement.country,
        language: agreement.language,
        languageName: agreement.languageName,
        mainTitle: agreement.mainTitle,
        subTitle: agreement.subTitle,
        mainContent: agreement.mainContent,
        subContent: agreement.subContent,
        agreeButtonText: agreement.agreeButtonText,
        disagreeButtonText: agreement.disagreeButtonText,
      });
      // Set available languages for the selected country
      if (agreement.country && countriesLanguages.countries[agreement.country as keyof typeof countriesLanguages.countries]) {
        const countryData = countriesLanguages.countries[agreement.country as keyof typeof countriesLanguages.countries];
        setAvailableLanguages(
          Object.entries(countryData.languages).map(([code, langData]) => ({
            code,
            name: (langData as { native: string; english: string }).english,
            native: (langData as { native: string; english: string }).native
          }))
        );
      }
    } else {
      setFormData({
        type: "NINTENDO-NETWORK-EULA",
        version: 1,
        country: "",
        language: "",
        languageName: "",
        mainTitle: "",
        subTitle: "",
        mainContent: "",
        subContent: "",
        agreeButtonText: "",
        disagreeButtonText: "",
      });
      setAvailableLanguages([]);
    }
  }, [agreement, open]);

  const handleCountryChange = (country: string) => {
    setFormData({ ...formData, country, language: "", languageName: "" });

    if (countriesLanguages.countries[country as keyof typeof countriesLanguages.countries]) {
      const countryData = countriesLanguages.countries[country as keyof typeof countriesLanguages.countries];
      setAvailableLanguages(
        Object.entries(countryData.languages).map(([code, langData]) => ({
          code,
          name: (langData as { native: string; english: string }).english,
          native: (langData as { native: string; english: string }).native
        }))
      );
    } else {
      setAvailableLanguages([]);
    }
  };

  const handleLanguageChange = (language: string) => {
    const selectedLang = availableLanguages.find(l => l.code === language);
    setFormData({
      ...formData,
      language,
      languageName: (selectedLang as any)?.native || ""
    });
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    // Aditional check for required fields
    if (!formData.type || !formData.country || !formData.language || !formData.version ||
        !formData.mainTitle || !formData.subTitle || !formData.mainContent ||
        !formData.subContent || !formData.agreeButtonText || !formData.disagreeButtonText) {
      alert("All fields are required. Please fill in all fields before saving.");
      return;
    }

    setIsSaving(true);
    try {
      await onSave(formData as Agreement);
      onOpenChange(false);
    } catch (error) {
      console.error("Error saving agreement:", error);
      alert("Failed to save agreement");
    } finally {
      setIsSaving(false);
    }
  };

  const countries = Object.keys(countriesLanguages.countries).sort();

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-3xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>{agreement ? "Edit Agreement" : "Add Agreement"}</DialogTitle>
          <DialogDescription>
            Configure a user agreement for a specific country and language
          </DialogDescription>
        </DialogHeader>

        <form onSubmit={handleSubmit} className="space-y-4">
          <div className="grid grid-cols-2 gap-4">
            <div className="space-y-2">
              <Label htmlFor="type">Agreement Type <span className="text-destructive">*</span></Label>
              <Select
                value={formData.type}
                onValueChange={(value) => setFormData({ ...formData, type: value })}
                required
              >
                <SelectTrigger id="type">
                  <SelectValue placeholder="Select agreement type" />
                </SelectTrigger>
                <SelectContent>
                  {AGREEMENT_TYPES.map((type) => (
                    <SelectItem key={type.value} value={type.value}>
                      {type.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div className="space-y-2">
              <Label htmlFor="version">Version <span className="text-destructive">*</span></Label>
              <Input
                id="version"
                type="number"
                min="1"
                max="9999"
                value={formData.version}
                onChange={(e) => setFormData({ ...formData, version: parseInt(e.target.value) || 1 })}
                required
              />
            </div>
          </div>

          <div className="grid grid-cols-2 gap-4">
            <div className="space-y-2">
              <Label htmlFor="country">Country <span className="text-destructive">*</span></Label>
              <Select
                value={formData.country}
                onValueChange={handleCountryChange}
                required
              >
                <SelectTrigger id="country">
                  <SelectValue placeholder="Select a country" />
                </SelectTrigger>
                <SelectContent>
                  {countries.map((country) => {
                    const countryData = countriesLanguages.countries[country as keyof typeof countriesLanguages.countries];
                    return (
                      <SelectItem key={country} value={country}>
                        {country} - {countryData.name}
                      </SelectItem>
                    );
                  })}
                </SelectContent>
              </Select>
            </div>

            <div className="space-y-2">
              <Label htmlFor="language">Language <span className="text-destructive">*</span></Label>
              <Select
                value={formData.language}
                onValueChange={handleLanguageChange}
                disabled={!formData.country}
                required
              >
                <SelectTrigger id="language">
                  <SelectValue placeholder="Select a language" />
                </SelectTrigger>
                <SelectContent>
                  {availableLanguages.map((lang) => (
                    <SelectItem key={lang.code} value={lang.code}>
                      {lang.code.toUpperCase()} - {lang.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          </div>

          <div className="space-y-2">
            <Label htmlFor="mainTitle">Main Title <span className="text-destructive">*</span></Label>
            <Input
              id="mainTitle"
              value={formData.mainTitle}
              onChange={(e) => setFormData({ ...formData, mainTitle: e.target.value })}
              placeholder="e.g., Nintendo Network Services Agreement"
              required
            />
          </div>

          <div className="space-y-2">
            <Label htmlFor="subTitle">Secondary Content Title <span className="text-destructive">*</span></Label>
            <Input
              id="subTitle"
              value={formData.subTitle}
              onChange={(e) => setFormData({ ...formData, subTitle: e.target.value })}
              placeholder="e.g., Privacy Policy"
              required
            />
          </div>

          <div className="space-y-2">
            <Label htmlFor="mainContent">Main Content <span className="text-destructive">*</span></Label>
            <Textarea
              id="mainContent"
              value={formData.mainContent}
              onChange={(e) => setFormData({ ...formData, mainContent: e.target.value })}
              placeholder="Enter the main agreement text..."
              rows={6}
              required
            />
          </div>

          <div className="space-y-2">
            <Label htmlFor="subContent">Secondary Content <span className="text-destructive">*</span></Label>
            <Textarea
              id="subContent"
              value={formData.subContent}
              onChange={(e) => setFormData({ ...formData, subContent: e.target.value })}
              placeholder="Enter additional agreement text..."
              rows={6}
              required
            />
          </div>

          <div className="grid grid-cols-2 gap-4">
            <div className="space-y-2">
              <Label htmlFor="agreeButtonText">Agree Button Text <span className="text-destructive">*</span></Label>
              <Input
                id="agreeButtonText"
                value={formData.agreeButtonText}
                onChange={(e) => setFormData({ ...formData, agreeButtonText: e.target.value })}
                placeholder="e.g., I Agree"
                required
              />
            </div>

            <div className="space-y-2">
              <Label htmlFor="disagreeButtonText">Disagree Button Text <span className="text-destructive">*</span></Label>
              <Input
                id="disagreeButtonText"
                value={formData.disagreeButtonText}
                onChange={(e) => setFormData({ ...formData, disagreeButtonText: e.target.value })}
                placeholder="e.g., I Disagree"
                required
              />
            </div>
          </div>

          <DialogFooter>
            <Button type="button" variant="outline" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button type="submit" disabled={isSaving}>
              {isSaving ? "Saving..." : "Save Agreement"}
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  );
}

