import type {Metadata} from 'next';
import './globals.css';
export const metadata: Metadata = {title: 'Corridor | Research execution console', description: 'Fort simulator observation and local execution workspace. Research scope only.'};
export default function RootLayout({children}: {children: React.ReactNode}) { return <html lang="en"><body>{children}</body></html>; }
