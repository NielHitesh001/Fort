import * as React from 'react';
import { Slot } from '@radix-ui/react-slot';
import { cva, type VariantProps } from 'class-variance-authority';
import { cn } from '@/lib/utils';
export const buttonVariants = cva('inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-sm text-xs font-medium transition-colors focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-[#9db3d6] disabled:pointer-events-none disabled:opacity-40 [&_svg]:size-3.5', {variants: {variant: {default: 'bg-[#b9cbed] text-[#111927] hover:bg-[#d2def4]', outline: 'border border-[#344152] bg-transparent text-[#d1d9e6] hover:bg-[#1a2636]', ghost: 'text-[#a3afbf] hover:bg-[#1a2636]'}, size: {default: 'h-9 px-4', sm: 'h-8 px-3', icon: 'size-9'}}, defaultVariants: {variant: 'default', size: 'default'}});
export function Button({className, variant, size, asChild = false, ...props}: React.ComponentProps<'button'> & VariantProps<typeof buttonVariants> & {asChild?: boolean}) { const Comp = asChild ? Slot : 'button'; return <Comp className={cn(buttonVariants({variant, size, className}))} {...props}/>; }
