git ls-files -co --exclude-standard ./ | grep '\.plot$' | xargs git add
git ls-files -co --exclude-standard ./tests/scripts/runs | grep '\.sh$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.c$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.h$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.py$' | xargs git add
git ls-files -co --exclude-standard ./ | grep '\.am$' | xargs git add
