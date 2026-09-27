import type { NextConfig } from 'next';
const config: NextConfig = { turbopack: {root: import.meta.dirname}, poweredByHeader: false, devIndicators: false };
export default config;
