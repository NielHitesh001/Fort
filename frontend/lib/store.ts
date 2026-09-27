import {create} from 'zustand';
export const useConsole = create<{palette: boolean; setPalette: (open: boolean) => void; orderId: string; setOrderId: (id: string) => void}>((set) => ({palette: false, setPalette: palette => set({palette}), orderId: '', setOrderId: orderId => set({orderId})}));
