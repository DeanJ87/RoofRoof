// The languages RoofRoof ships: English only for now (spec 001). The i18n
// framework from SQMeter is kept, so another language is a messages file and
// an entry here. Names are each language's own name for itself.

export const LANGUAGES = [{ code: 'en', name: 'English' }] as const;

// A BCP 47 tag: what setLanguage and the formatters accept (tests use other
// locales to check number formatting).
export type LanguageCode = string;

const RTL: readonly string[] = ['ar'];

export const isRtl = (code: string) => RTL.includes(code);

export const isLanguageCode = (code: unknown): code is LanguageCode => LANGUAGES.some((language) => language.code === code);
