import pandas as pd
import glob

# List of CSV files to read
csv_files = glob.glob('*.csv')

# Columns to extract from each CSV file
columns_to_extract = ['bbs', 'n', 'id', 'search', 'search_false_pos']

# List to store extracted dataframes
dfs = []

# Read each CSV file and extract specific columns
for file in csv_files:
    # print(file)
    df = pd.read_csv(file, usecols=columns_to_extract)
    # dfs.append(df)
    df.to_csv(f'extracted-{file}.csv', index=True)

# Concatenate all dataframes into one
# combined_df = pd.concat(dfs, ignore_index=True)

# Write the combined dataframe to a new CSV file