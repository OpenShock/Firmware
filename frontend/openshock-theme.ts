import type { CustomThemeConfig } from '@skeletonlabs/tw-plugin';

export const openshockTheme: CustomThemeConfig = {
  name: 'openshock',
  properties: {
    // =~= Theme Properties =~=
    '--theme-font-family-base': `system-ui`,
    '--theme-font-family-heading': `system-ui`,
    '--theme-font-color-base': '0 0 0',
    '--theme-font-color-dark': '255 255 255',
    '--theme-rounded-base': '9999px',
    '--theme-rounded-container': '12px',
    '--theme-border-base': '1px',
    // =~= Theme On-X Colors =~=
    '--on-primary': '0 0 0',
    '--on-secondary': '255 255 255',
    '--on-tertiary': '0 0 0',
    '--on-success': '0 0 0',
    '--on-warning': '0 0 0',
    '--on-error': '255 255 255',
    '--on-surface': '255 255 255',
    // =~= Theme Colors  =~=
    // primary | #ff5c82
    '--color-primary-50': '255 231 236', // #ffe7ec
    '--color-primary-100': '255 222 230', // #ffdee6
    '--color-primary-200': '255 214 224', // #ffd6e0
    '--color-primary-300': '255 190 205', // #ffbecd
    '--color-primary-400': '255 141 168', // #ff8da8
    '--color-primary-500': '255 92 130', // #ff5c82
    '--color-primary-600': '230 83 117', // #e65375
    '--color-primary-700': '191 69 98', // #bf4562
    '--color-primary-800': '153 55 78', // #99374e
    '--color-primary-900': '125 45 64', // #7d2d40
    // secondary | #463eda
    '--color-secondary-50': '227 226 249', // #e3e2f9
    '--color-secondary-100': '218 216 248', // #dad8f8
    '--color-secondary-200': '209 207 246', // #d1cff6
    '--color-secondary-300': '181 178 240', // #b5b2f0
    '--color-secondary-400': '126 120 229', // #7e78e5
    '--color-secondary-500': '70 62 218', // #463eda
    '--color-secondary-600': '63 56 196', // #3f38c4
    '--color-secondary-700': '53 47 164', // #352fa4
    '--color-secondary-800': '42 37 131', // #2a2583
    '--color-secondary-900': '34 30 107', // #221e6b
    // tertiary | #33b8ff
    '--color-tertiary-50': '224 244 255', // #e0f4ff
    '--color-tertiary-100': '214 241 255', // #d6f1ff
    '--color-tertiary-200': '204 237 255', // #ccedff
    '--color-tertiary-300': '173 227 255', // #ade3ff
    '--color-tertiary-400': '112 205 255', // #70cdff
    '--color-tertiary-500': '51 184 255', // #33b8ff
    '--color-tertiary-600': '46 166 230', // #2ea6e6
    '--color-tertiary-700': '38 138 191', // #268abf
    '--color-tertiary-800': '31 110 153', // #1f6e99
    '--color-tertiary-900': '25 90 125', // #195a7d
    // success | #03c200
    '--color-success-50': '217 246 217', // #d9f6d9
    '--color-success-100': '205 243 204', // #cdf3cc
    '--color-success-200': '192 240 191', // #c0f0bf
    '--color-success-300': '154 231 153', // #9ae799
    '--color-success-400': '79 212 77', // #4fd44d
    '--color-success-500': '3 194 0', // #03c200
    '--color-success-600': '3 175 0', // #03af00
    '--color-success-700': '2 146 0', // #029200
    '--color-success-800': '2 116 0', // #027400
    '--color-success-900': '1 95 0', // #015f00
    // warning | #ff7300
    '--color-warning-50': '255 234 217', // #ffead9
    '--color-warning-100': '255 227 204', // #ffe3cc
    '--color-warning-200': '255 220 191', // #ffdcbf
    '--color-warning-300': '255 199 153', // #ffc799
    '--color-warning-400': '255 157 77', // #ff9d4d
    '--color-warning-500': '255 115 0', // #ff7300
    '--color-warning-600': '230 104 0', // #e66800
    '--color-warning-700': '191 86 0', // #bf5600
    '--color-warning-800': '153 69 0', // #994500
    '--color-warning-900': '125 56 0', // #7d3800
    // error | #a30000
    '--color-error-50': '241 217 217', // #f1d9d9
    '--color-error-100': '237 204 204', // #edcccc
    '--color-error-200': '232 191 191', // #e8bfbf
    '--color-error-300': '218 153 153', // #da9999
    '--color-error-400': '191 77 77', // #bf4d4d
    '--color-error-500': '163 0 0', // #a30000
    '--color-error-600': '147 0 0', // #930000
    '--color-error-700': '122 0 0', // #7a0000
    '--color-error-800': '98 0 0', // #620000
    '--color-error-900': '80 0 0', // #500000
    // surface | #202325
    '--color-surface-50': '222 222 222', // #dedede
    '--color-surface-100': '210 211 211', // #d2d3d3
    '--color-surface-200': '199 200 201', // #c7c8c9
    '--color-surface-300': '166 167 168', // #a6a7a8
    '--color-surface-400': '99 101 102', // #636566
    '--color-surface-500': '32 35 37', // #202325
    '--color-surface-600': '29 32 33', // #1d2021
    '--color-surface-700': '24 26 28', // #181a1c
    '--color-surface-800': '19 21 22', // #131516
    '--color-surface-900': '16 17 18', // #101112
  },
};
