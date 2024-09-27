const path = require('path');

module.exports = {
  target: 'node',
  mode: 'production',
  entry: {
    'get-vars': './src/get-vars.ts',
    's3-upload': './src/s3-upload.ts',
  },
  module: {
    rules: [
      {
        test: /\.ts$/,
        use: 'ts-loader',
      },
    ],
  },
  resolve: {
    extensions: ['.ts', '.js'],
  },
  optimization: {
    minimize: true,
    innerGraph: true,
    usedExports: true,
    mangleExports: 'size',
  },
  output: {
    filename: '[name].js',
    path: path.resolve(__dirname, 'dist'),
  },
};
