// Where the web UI links out to the docs. One constant, so a move of the docs
// (a public docs site, once there is one) changes one line (SQMeter spec 028-openapi research D6).
// There is no public docs host yet: the repository is the one stable address.
export const DOCS_URL = 'https://github.com/DeanJ87/RoofRoof/';

/** A docs page in the repository, e.g. `docsLink('hardware/commissioning.md', 'stage-11-the-gate-for-unattended-use')`. */
export const docsLink = (page: string, anchor?: string) => `${DOCS_URL}blob/main/docs/${page}${anchor ? `#${anchor}` : ''}`;

/** How N.I.N.A. opens and closes the roof over Alpaca (spec 014): one anchor for Settings → Roof and the setup wizard. */
export const NINA_ROOF_DOC = docsLink('user-guide/alpaca.md', 'nina-opens-and-closes-the-roof');
